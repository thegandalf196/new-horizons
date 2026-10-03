/*
 * NewHorizonsLearningMentorAITest.cpp, part of VCMI engine
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

class MentorRecordingServer final : public GameHandlerTestServer
{
public:
	using GameHandlerTestServer::GameHandlerTestServer;

	std::vector<std::pair<ObjectInstanceID, int32_t>> mentorStatePacks;

	void applyPack(CPackForClient & pack) override
	{
		if(const auto * state = dynamic_cast<const SetNewHorizonsLearningMentorState *>(&pack))
			mentorStatePacks.emplace_back(state->heroId, state->lastUseWeek);
		GameHandlerTestServer::applyPack(pack);
	}
};

class NewHorizonsLearningMentorAITest : public NullkillerTest
{
protected:
	void SetUp() override
	{
		NullkillerTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
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

	void makeMentorTheOnlyActivePerk(CGHeroInstance * hero)
	{
		auto & state = const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState());
		ASSERT_TRUE(newHorizonsHeroes::usesPerkRules(state.rules));
		const auto mentor = newHorizonsHeroes::perkDefinition(state.rules, LEARNING_SKILL, MENTOR_PERK);
		ASSERT_TRUE(mentor.has_value());
		ASSERT_EQ(mentor->effect["status"].String(), "active");

		// Keep the saved registry structurally canonical while making this real
		// level-up offer unambiguous across unrelated active perk families.
		for(auto & skillEntry : state.rules["skills"].Struct())
		{
			auto & skill = skillEntry.second;
			for(auto & perk : skill["perks"].Vector())
			{
				if(perk["id"].String() != MENTOR_PERK)
					perk["effect"]["status"].String() = "planned";
			}
		}
		state.validate();
	}

	void makeFieldStudyTheOnlyAdvancedOffer(CGHeroInstance * hero)
	{
		auto & state = const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState());
		ASSERT_TRUE(newHorizonsHeroes::usesPerkRules(state.rules));
		ASSERT_TRUE(newHorizonsHeroes::perkDefinition(state.rules, LEARNING_SKILL, MENTOR_PERK).has_value());
		ASSERT_TRUE(newHorizonsHeroes::perkDefinition(state.rules, LEARNING_SKILL, FIELD_STUDY_PERK).has_value());

		// The production registry intentionally keeps Field Study planned until
		// its principal native gate passes. Activate only this saved catalog row
		// to exercise the real legal Advanced offer and Nullkiller query path.
		for(auto & skillEntry : state.rules["skills"].Struct())
		{
			auto & skill = skillEntry.second;
			for(auto & perk : skill["perks"].Vector())
			{
				const auto & id = perk["id"].String();
				if(id == MENTOR_PERK || id == FIELD_STUDY_PERK)
					perk["effect"]["status"].String() = "active";
				else
					perk["effect"]["status"].String() = "planned";
			}
		}
		state.validate();
	}

	CGHeroInstance * mentor = nullptr;
	CGHeroInstance * recipient = nullptr;
};
} // namespace

TEST_F(NewHorizonsLearningMentorAITest, NullkillerSelectsMentorAndTheAuthoritativeMeetingAwardsLearningAdjustedXp)
{
	startGame();
	MentorRecordingServer server(gameState(), AI_PLAYER);
	CGameHandler handler(server, gameState());
	gameState()->actingPlayers.insert(AI_PLAYER);

	const SecondarySkill learning(SecondarySkill::decode(LEARNING_SKILL));
	ASSERT_GE(learning.getNum(), 0);
	handler.changeSecSkill(mentor, learning, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	handler.changeSecSkill(recipient, learning, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	makeMentorTheOnlyActivePerk(mentor);

	const auto initialMentorLevel = mentor->level;
	mentor->setExperience(LIBRARY->heroh->reqExp(initialMentorLevel + 1), ChangeValueMode::ABSOLUTE);
	handler.levelUpHero(mentor);
	auto query = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(handler.queries->topQuery(AI_PLAYER));
	ASSERT_NE(query, nullptr);
	ASSERT_TRUE(query->hlu.skills.empty());
	ASSERT_EQ(query->hlu.perks.size(), 1u);
	EXPECT_EQ(query->hlu.perks.front().selection.skillId, LEARNING_SKILL);
	EXPECT_EQ(query->hlu.perks.front().selection.perkId, MENTOR_PERK);
	EXPECT_EQ(query->hlu.perks.front().requiredRank, 1);
	handler.onAdvInterfaceReady(AI_PLAYER);
	ASSERT_TRUE(query->prompted);

	const auto outcome = askNullkillerToChoose(handler, mentor, query);
	ASSERT_EQ(outcome.status, std::future_status::ready) << "Nullkiller did not answer the Mentor level-up query";
	ASSERT_TRUE(outcome.result.accepted) << outcome.result.error;
	EXPECT_EQ(outcome.result.choice, 0);
	EXPECT_EQ(mentor->level, initialMentorLevel + 1);
	ASSERT_EQ(mentor->getPerkState().selected.size(), 1u);
	EXPECT_EQ(mentor->getPerkState().selected.front(), (newHorizonsHeroes::PerkSelection{LEARNING_SKILL, MENTOR_PERK}));
	ASSERT_TRUE(mentor->hasActivePerk(LEARNING_SKILL, MENTOR_PERK));

	ASSERT_GT(mentor->level, recipient->level);
	const auto calendar = gameState()->getCalendar();
	const int week = newHorizonsMuster::absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek());
	EXPECT_FALSE(mentor->hasUsedNewHorizonsLearningMentor(week));
	const auto mentorBaseExperience = static_cast<TExpType>(250 * mentor->level);
	const auto expectedAward = recipient->calculateXp(mentorBaseExperience);
	ASSERT_GT(expectedAward, mentorBaseExperience) << "The recipient's ordinary Learning rank should compose with Mentor";
	const auto experienceBeforeMeeting = recipient->exp;

	handler.heroExchange(mentor->id, recipient->id);

	EXPECT_EQ(recipient->exp - experienceBeforeMeeting, expectedAward);
	ASSERT_EQ(server.mentorStatePacks.size(), 1u);
	EXPECT_EQ(server.mentorStatePacks.front(), std::make_pair(mentor->id, static_cast<int32_t>(week)));
	EXPECT_EQ(mentor->getNewHorizonsLearningMentorLastWeek(), week);
	EXPECT_TRUE(mentor->hasUsedNewHorizonsLearningMentor(week));
}

TEST_F(NewHorizonsLearningMentorAITest, NullkillerSelectsTheLegallyOfferedAdvancedFieldStudyPerk)
{
	startGame();
	GameHandlerTestServer server(gameState(), AI_PLAYER);
	CGameHandler handler(server, gameState());
	gameState()->actingPlayers.insert(AI_PLAYER);

	const SecondarySkill learning(SecondarySkill::decode(LEARNING_SKILL));
	ASSERT_GE(learning.getNum(), 0);
	handler.changeSecSkill(mentor, learning, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	mentor->applyPerkSelection({LEARNING_SKILL, MENTOR_PERK});
	handler.changeSecSkill(mentor, learning, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	makeFieldStudyTheOnlyAdvancedOffer(mentor);

	const auto rankLookup = [this](const std::string & skillId)
	{
		return mentor->getPerkSkillRank(skillId);
	};
	const auto legalOffer = mentor->getPerkState().prepareOffer(rankLookup, 0);
	ASSERT_EQ(legalOffer.size(), 1u);
	EXPECT_EQ(legalOffer.front().selection, (newHorizonsHeroes::PerkSelection{LEARNING_SKILL, FIELD_STUDY_PERK}));
	EXPECT_EQ(legalOffer.front().requiredRank, MasteryLevel::ADVANCED);

	const auto initialMentorLevel = mentor->level;
	mentor->setExperience(LIBRARY->heroh->reqExp(initialMentorLevel + 1), ChangeValueMode::ABSOLUTE);
	handler.levelUpHero(mentor);
	auto query = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(handler.queries->topQuery(AI_PLAYER));
	ASSERT_NE(query, nullptr);
	ASSERT_TRUE(query->hlu.skills.empty());
	ASSERT_EQ(query->hlu.perks.size(), 1u);
	EXPECT_EQ(query->hlu.perks.front().selection, legalOffer.front().selection);
	EXPECT_EQ(query->hlu.perks.front().requiredRank, MasteryLevel::ADVANCED);
	handler.onAdvInterfaceReady(AI_PLAYER);
	ASSERT_TRUE(query->prompted);

	const auto outcome = askNullkillerToChoose(handler, mentor, query);
	ASSERT_EQ(outcome.status, std::future_status::ready) << "Nullkiller did not answer the Field Study level-up query";
	ASSERT_TRUE(outcome.result.accepted) << outcome.result.error;
	EXPECT_EQ(outcome.result.choice, 0);
	EXPECT_EQ(mentor->level, initialMentorLevel + 1);
	ASSERT_EQ(mentor->getPerkState().selected.size(), 2u);
	EXPECT_EQ(mentor->getPerkState().selected.back(),
		(newHorizonsHeroes::PerkSelection{LEARNING_SKILL, FIELD_STUDY_PERK}));
	EXPECT_TRUE(mentor->hasActivePerk(LEARNING_SKILL, FIELD_STUDY_PERK));
}
