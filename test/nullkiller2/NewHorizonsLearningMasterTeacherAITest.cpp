/*
 * NewHorizonsLearningMasterTeacherAITest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"

#include <chrono>
#include <future>
#include <mutex>

#include "lib/GameConstants.h"
#include "lib/GameLibrary.h"
#include "lib/IGameSettings.h"
#include "lib/callback/AIFactory.h"
#include "lib/callback/CCallback.h"
#include "lib/callback/CGlobalAI.h"
#include "lib/callback/IClient.h"
#include "lib/entities/creature/NewHorizonsMusterRules.h"
#include "lib/entities/hero/CHeroHandler.h"
#include "lib/entities/hero/NewHorizonsPerkRules.h"
#include "lib/entities/hero/NewHorizonsPerkState.h"
#include "lib/gameState/CGameState.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/modding/CModHandler.h"
#include "lib/networkPacks/PacksForClient.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/serializer/CTypeList.h"
#include "server/CGameHandler.h"
#include "server/queries/QueriesProcessor.h"
#include "server/ServerNetPackVisitors.h"
#include "server/queries/MapQueries.h"
#include "mock/GameHandlerTestServer.h"
#include "mock/TinyH3MBuilder.h"
#include "nullkiller2/NullkillerTest.h"

namespace
{
const PlayerColor HUMAN_PLAYER(0);
const PlayerColor AI_PLAYER(1);
const std::string LEARNING_SKILL = "new-horizons:learning";
const std::string MENTOR_PERK = "new-horizons:learning.mentor";
const std::string MASTER_TEACHER_PERK = "new-horizons:learning.masterTeacher";
const std::string HISTORIAN_PERK = "new-horizons:learning.historian";
const std::string FIELD_STUDY_PERK = "new-horizons:learning.fieldStudy";

class MentorAiEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit MentorAiEnvironment(std::shared_ptr<CGameState> state)
		: state(std::move(state))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class MentorQueryLoopback final : public IClient
{
	CGameHandler & handler;
	std::once_flag completion;

public:
	struct Result
	{
		int choice = -1;
		bool accepted = false;
		std::string error;
	};

	std::weak_ptr<CGlobalAI> ai;
	std::promise<Result> completed;

	explicit MentorQueryLoopback(CGameHandler & handler)
		: handler(handler)
	{}

	void complete(Result result) noexcept
	{
		try
		{
			std::call_once(completion, [this, result = std::move(result)]() mutable
			{
				try
				{
					completed.set_value(std::move(result));
				}
				catch(...)
				{
					// Keep a transport error from escaping the Nullkiller worker.
				}
			});
		}
		catch(...)
		{
			// Keep a transport error from escaping the Nullkiller worker.
		}
	}

	std::optional<BattleAction> makeSurrenderRetreatDecision(
		PlayerColor,
		const BattleID &,
		const BattleStateInfoForRetreat &) override
	{
		return std::nullopt;
	}

	int sendRequest(const CPackForServer & outgoing, PlayerColor player, bool waitTillRealize) override
	{
		constexpr int requestID = 31;
		try
		{
			const auto * reply = dynamic_cast<const QueryReply *>(&outgoing);
			if(!reply || reply->player != player || !waitTillRealize)
				throw std::runtime_error("Unexpected Learning Mentor AI request");

			QueryReply pack = *reply;
			pack.requestID = requestID;
			const auto controller = ai.lock();
			if(!controller)
				throw std::runtime_error("Missing Nullkiller controller");
			controller->requestSent(&pack, requestID);

			ApplyGhNetPackVisitor visitor(handler, GameConnectionID::FIRST_CONNECTION);
			pack.visitTyped(visitor);
			PackageApplied ack;
			ack.player = player;
			ack.requestID = requestID;
			ack.packType = CTypeList::getInstance().getTypeID<QueryReply>(nullptr);
			ack.result = visitor.getResult();
			controller->requestRealized(&ack);
			complete({pack.reply.value_or(-1), visitor.getResult(), {}});
		}
		catch(const std::exception & error)
		{
			complete({-1, false, error.what()});
		}
		catch(...)
		{
			complete({-1, false, "Unknown Learning Mentor AI transport failure"});
		}
		return requestID;
	}
};

class NewHorizonsLearningMasterTeacherAITest : public NullkillerTest
{
protected:
	void SetUp() override
	{
		NullkillerTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			FAIL() << "Requires the isolated New Horizons test preset";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		auto perks = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		const auto & catalog = perks["skills"][LEARNING_SKILL]["perks"].Vector();
		const auto registered = std::find_if(catalog.begin(), catalog.end(), [](const auto & perk)
		{
			return perk["id"].String() == MASTER_TEACHER_PERK;
		});
		ASSERT_NE(registered, catalog.end());
		ASSERT_EQ((*registered)["effect"]["status"].String(), "active");
		RecordProperty("registry_status", "active");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perks));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_MASTERIES, JsonNode());
		// A legal scenario option with no secondary-skill offers isolates the
		// generic AI fallback that chooses a server-authored perk offer.
		loaded->overrideGameSetting(EGameSettings::LEVEL_UP_TOTAL_SKILLS_AMOUNT, JsonNode(0));
	}

	void configurePlayer(PlayerSettings & settings) const override
	{
		if(settings.color == AI_PLAYER)
			settings.connectedPlayerIDs.clear();
	}

	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false)
			.playerActive(HUMAN_PLAYER)
			.playerActive(AI_PLAYER)
			.hero({5, 5, 0}, HeroTypeID(0), AI_PLAYER)
			.hero({9, 5, 0}, HeroTypeID(1), AI_PLAYER);
		startWithMap(std::move(builder));

		mentor = findHeroAt({5, 5, 0});
		recipient = findHeroAt({9, 5, 0});
		ASSERT_NE(mentor, nullptr);
		ASSERT_NE(recipient, nullptr);
		ASSERT_EQ(mentor->getOwner(), AI_PLAYER);
		ASSERT_EQ(recipient->getOwner(), AI_PLAYER);
	}

	struct LevelUpChoice
	{
		std::future_status status;
		MentorQueryLoopback::Result result;
	};

	LevelUpChoice askNullkillerToChoose(CGameHandler & handler, CGHeroInstance * hero,
		const std::shared_ptr<CHeroLevelUpDialogQuery> & query)
	{
		auto transport = std::make_shared<MentorQueryLoopback>(handler);
		const auto callback = makeCallback(AI_PLAYER, transport.get());
		const auto ai = AIFactory::createAdventureAI("Nullkiller2");
		if(!ai)
			return {std::future_status::deferred, {-1, false, "Missing Nullkiller controller"}};
		transport->ai = ai;
		ai->initGameInterface(std::make_shared<MentorAiEnvironment>(gameState()), callback);
		auto answer = transport->completed.get_future();
		ai->heroGotLevel(hero, query->hlu.primskill, query->hlu.skills, query->hlu.perks, query->queryID);
		const auto ready = answer.wait_for(std::chrono::seconds(10));
		ai->finish();
		return {ready, ready == std::future_status::ready ? answer.get() : MentorQueryLoopback::Result{}};
	}

	void isolateLearningOffers(CGHeroInstance * hero)
	{
		auto & state = const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState());
		for(auto & skillEntry : state.rules["skills"].Struct())
		{
			for(auto & perk : skillEntry.second["perks"].Vector())
			{
				const auto & id = perk["id"].String();
				if(id == MASTER_TEACHER_PERK || id == FIELD_STUDY_PERK)
				{
					ASSERT_EQ(perk["effect"]["status"].String(), "active");
					continue; // Preserve actual production registration, never activate Teacher.
				}
				// Test-only passive prerequisite catalog: no active non-Mentor Basic
				// Learning choice exists yet. No Historian mechanic is certified here.
				perk["effect"]["status"].String() = id == HISTORIAN_PERK ? "active" : "planned";
			}
		}
		state.validate();
		RecordProperty("standalone_prerequisite", "captured-test-only Historian Basic tier");
	}

	void acceptLowerPerk(CGameHandler & handler, const std::string & id)
	{
		const auto rankLookup = [this](const std::string & skill) { return mentor->getPerkSkillRank(skill); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = mentor->getPerkState().prepareOffer(rankLookup, seed);
			const auto found = std::find_if(offers.begin(), offers.end(), [&id](const auto & offer)
			{
				return offer.selection.skillId == LEARNING_SKILL && offer.selection.perkId == id;
			});
			if(found == offers.end())
				continue;
			handler.levelUpHero(mentor, offers, std::distance(offers.begin(), found), seed, false);
			ASSERT_TRUE(mentor->hasActivePerk(LEARNING_SKILL, id));
			return;
		}
		FAIL() << "No legal lower-tier Learning offer";
	}

	CGHeroInstance * mentor = nullptr;
	CGHeroInstance * recipient = nullptr;
};
} // namespace


TEST_F(NewHorizonsLearningMasterTeacherAITest, NullkillerChoosesLegalExpertAndMeetingForwardsAuthoritativeAward)
{
	startGame();
	GameHandlerTestServer server(gameState(), AI_PLAYER);
	CGameHandler handler(server, gameState());
	gameState()->actingPlayers.insert(AI_PLAYER);
	const SecondarySkill learning(SecondarySkill::decode(LEARNING_SKILL));
	ASSERT_GE(learning.getNum(), 0);
	handler.changeSecSkill(mentor, learning, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	isolateLearningOffers(mentor);
	ASSERT_NO_FATAL_FAILURE(acceptLowerPerk(handler, HISTORIAN_PERK));
	handler.changeSecSkill(mentor, learning, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	isolateLearningOffers(mentor);
	ASSERT_NO_FATAL_FAILURE(acceptLowerPerk(handler, FIELD_STUDY_PERK));
	handler.changeSecSkill(mentor, learning, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	isolateLearningOffers(mentor);
	const auto initialLevel = mentor->level;
	mentor->setExperience(LIBRARY->heroh->reqExp(initialLevel + 1), ChangeValueMode::ABSOLUTE);
	handler.levelUpHero(mentor);
	const auto query = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(handler.queries->topQuery(AI_PLAYER));
	ASSERT_NE(query, nullptr);
	ASSERT_TRUE(query->hlu.skills.empty());
	ASSERT_EQ(query->hlu.perks.size(), 1u);
	EXPECT_EQ(query->hlu.perks.front().selection,
		(newHorizonsHeroes::PerkSelection{LEARNING_SKILL, MASTER_TEACHER_PERK}));
	EXPECT_EQ(query->hlu.perks.front().requiredRank, MasteryLevel::EXPERT);
	handler.onAdvInterfaceReady(AI_PLAYER);
	ASSERT_TRUE(query->prompted);
	const auto outcome = askNullkillerToChoose(handler, mentor, query);
	ASSERT_EQ(outcome.status, std::future_status::ready);
	ASSERT_TRUE(outcome.result.accepted) << outcome.result.error;
	EXPECT_EQ(outcome.result.choice, 0);
	ASSERT_TRUE(mentor->hasActivePerk(LEARNING_SKILL, MASTER_TEACHER_PERK));
	ASSERT_FALSE(mentor->hasActivePerk(LEARNING_SKILL, MENTOR_PERK));
	ASSERT_GT(mentor->level, recipient->level);
	handler.changeSecSkill(recipient, learning, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	const auto before = recipient->exp;
	const auto expected = recipient->calculateXp(500 * mentor->level);
	handler.heroExchange(mentor->id, recipient->id);
	EXPECT_EQ(recipient->exp - before, expected);
	EXPECT_EQ(mentor->getNewHorizonsLearningMentorRecipients(),
		(CGHeroInstance::LearningMentorRecipients{recipient->id, ObjectInstanceID::NONE}));
	const auto after = recipient->exp;
	handler.heroExchange(mentor->id, recipient->id);
	EXPECT_EQ(recipient->exp, after);
}
