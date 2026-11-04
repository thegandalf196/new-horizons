/*
 * NewHorizonsLearningMasterTeacherTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/entities/hero/NewHorizonsLearningSpecialties.h"
#include "../../server/CGameHandler.h"
#include "../../server/queries/MapQueries.h"
#include "../../server/queries/QueriesProcessor.h"
#include "battles/FullGameSnapshotTypes.h"
#include "../mock/GameHandlerTestServer.h"
#include "../mock/TinyMapGameTest.h"

namespace
{
constexpr auto learningId = "new-horizons:learning";
constexpr auto teacherId = "new-horizons:learning.philosopher";
constexpr auto mentorId = "new-horizons:learning.mentor";
constexpr auto historianId = "new-horizons:learning.historian";
constexpr auto fieldStudyId = "new-horizons:learning.fieldStudy";
using Recipients = CGHeroInstance::LearningMentorRecipients;

class OrisExchangeMutationServer : public GameHandlerTestServer
{
public:
	using GameHandlerTestServer::GameHandlerTestServer;
	std::function<void()> onExchange;
	void applyPack(CPackForClient & pack) override
	{
		GameHandlerTestServer::applyPack(pack);
		if(dynamic_cast<ExchangeDialog *>(&pack) && onExchange)
			onExchange();
	}
};

class NewHorizonsLearningMasterTeacherTest : public TinyMapGameTest
{
protected:
	CGHeroInstance * teacher = nullptr;
	bool oris = false;
	bool customOrisMentor = false;
	bool specialtyCaptured = true;
	std::array<CGHeroInstance *, 3> recipients{};
	CGHeroInstance * enemy = nullptr;
	CGTownInstance * town = nullptr;

	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			<< "Run with the isolated New Horizons test preset";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		auto rules = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
		rules.setOverrideFlag(true);
		if(!specialtyCaptured)
			rules["additionalSpecialties"].Struct().erase("spiritTeacher");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		if(oris || customOrisMentor)
			for(const auto & object : loaded->objects)
				if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get()); hero && hero->pos == int3(5, 5, 0))
					hero->setHeroType(HeroTypeID(HeroTypeID::decode("core:oris")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		auto perks = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		const auto & catalog = perks["skills"][learningId]["perks"].Vector();
		const auto registered = std::find_if(catalog.begin(), catalog.end(), [](const auto & perk)
		{
			return perk["id"].String() == teacherId;
		});
		ASSERT_NE(registered, catalog.end());
		ASSERT_EQ((*registered)["effect"]["status"].String(), "active");
		RecordProperty("registry_status", "active");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perks));
	}

	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.town({12, 12, 0}, FactionID(FactionID::decode("core:castle")), PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0));
		if(customOrisMentor)
			// Authored custom skills bypass the default Eagle Eye starting profile.
			builder.heroSecondarySkills({{SecondarySkill::LEARNING, MasteryLevel::BASIC}});
		builder.hero({6, 6, 0}, HeroTypeID(1), PlayerColor(0))
			.hero({7, 7, 0}, HeroTypeID(2), PlayerColor(0))
			.hero({8, 8, 0}, HeroTypeID(3), PlayerColor(0))
			.hero({9, 9, 0}, HeroTypeID(4), PlayerColor(1));
		startWithMap(std::move(builder));
		teacher = findHeroAt({5, 5, 0});
		recipients = {findHeroAt({6, 6, 0}), findHeroAt({7, 7, 0}), findHeroAt({8, 8, 0})};
		enemy = findHeroAt({9, 9, 0});
		town = findFirst<CGTownInstance>();
		ASSERT_NE(teacher, nullptr);
		ASSERT_NE(enemy, nullptr);
		ASSERT_NE(town, nullptr);
		teacher->level = 10;
		if(customOrisMentor)
		{
			ASSERT_EQ(teacher->getHeroTypeID(), HeroTypeID(HeroTypeID::decode("core:oris")));
			ASSERT_FALSE(teacher->getPerkState().hasSelection(learningId, "new-horizons:learning.eagleEye"));
		}
		for(auto * recipient : recipients)
		{
			ASSERT_NE(recipient, nullptr);
			recipient->level = 1;
		}
	}

	void select(CGameHandler & handler, const char * id)
	{
		const auto rankLookup = [this](const std::string & skill) { return teacher->getPerkSkillRank(skill); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = teacher->getPerkState().prepareOffer(rankLookup, seed, teacher->getPerkOfferWeightContext());
			const auto found = std::find_if(offers.begin(), offers.end(), [id](const auto & offer)
			{
				return offer.selection.skillId == learningId && offer.selection.perkId == id;
			});
			if(found == offers.end())
				continue;
			handler.levelUpHero(teacher, offers, std::distance(offers.begin(), found), seed, false);
			ASSERT_TRUE(teacher->hasActivePerk(learningId, id));
			return;
		}
		FAIL() << "No legal offer for " << id;
	}

	void prepareExpert(CGameHandler & handler, bool withMentor = true)
	{
		const SecondarySkill learning(SecondarySkill::decode(learningId));
		ASSERT_GE(learning.getNum(), 0);
		handler.changeSecSkill(teacher, learning, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		if(!oris)
			ASSERT_NO_FATAL_FAILURE(select(handler, withMentor ? mentorId : historianId));
		handler.changeSecSkill(teacher, learning, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(select(handler, fieldStudyId));
		handler.changeSecSkill(teacher, learning, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(select(handler, teacherId));
		EXPECT_EQ(teacher->getNewHorizonsLearningMentorExperiencePerLevel(), withMentor && !oris ? 250 : 0);
	}

	void settleCurrentQueries(CGameHandler & handler)
	{
		handler.onAdvInterfaceReady(PlayerColor(0));
		for(int n = 0; n < 32; ++n)
		{
			const auto query = handler.queries->topQuery(PlayerColor(0));
			if(!query) return;
			ASSERT_TRUE(std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(query)
				|| std::dynamic_pointer_cast<CGarrisonDialogQuery>(query));
			ASSERT_TRUE(handler.queryReply(query->queryID, 0, PlayerColor(0)));
		}
		FAIL() << "Bounded current Mentor query settlement exceeded";
	}

};
}

TEST_F(NewHorizonsLearningMasterTeacherTest, ExpertLearningWithMentorAwardsOnlyOneRecipientAndRenewsNextWeek)
{
	startGame();
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	ASSERT_NO_FATAL_FAILURE(prepareExpert(handler));
	ASSERT_TRUE(teacher->hasActivePerk(learningId, mentorId));
	handler.changeSecSkill(recipients[0], SecondarySkill(SecondarySkill::decode(learningId)),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	const auto firstBefore = recipients[0]->exp;
	const auto expected = recipients[0]->calculateXp(250 * teacher->level);
	const auto levelAtMeeting = teacher->level;
	handler.heroExchange(teacher->id, recipients[0]->id);
	EXPECT_EQ(recipients[0]->exp - firstBefore, expected);
	EXPECT_EQ(teacher->level, levelAtMeeting);
	EXPECT_EQ(teacher->getNewHorizonsLearningMentorRecipients(), (Recipients{recipients[0]->id, ObjectInstanceID::NONE}));
	ASSERT_NE(std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(handler.queries->topQuery(PlayerColor(0))), nullptr);
	bool exchangeBelowLevelUp = false;
	for(const auto & query : handler.queries->allQueries())
	{
		const auto exchange = std::dynamic_pointer_cast<CGarrisonDialogQuery>(query);
		if(exchange && exchange->exchangingArmies[0] == teacher && exchange->exchangingArmies[1] == recipients[0])
			exchangeBelowLevelUp = true;
	}
	EXPECT_TRUE(exchangeBelowLevelUp);
	const auto firstAfter = recipients[0]->exp;
	handler.heroExchange(teacher->id, recipients[0]->id);
	EXPECT_EQ(recipients[0]->exp, firstAfter);
	const auto secondBefore = recipients[1]->exp;
	const TExpType secondExpected = 0;
	handler.heroExchange(recipients[1]->id, teacher->id);
	EXPECT_EQ(recipients[1]->exp - secondBefore, secondExpected);
	EXPECT_EQ(teacher->getNewHorizonsLearningMentorRecipients(), (Recipients{recipients[0]->id, ObjectInstanceID::NONE}));
	const auto thirdBefore = recipients[2]->exp;
	handler.heroExchange(teacher->id, recipients[2]->id);
	EXPECT_EQ(recipients[2]->exp, thirdBefore);
	gameState()->day = 8;
	const auto nextExpected = recipients[2]->calculateXp(250 * teacher->level);
	handler.heroExchange(teacher->id, recipients[2]->id);
	EXPECT_EQ(recipients[2]->exp - thirdBefore, nextExpected);
	EXPECT_EQ(teacher->getNewHorizonsLearningMentorLastWeek(), 1);
	EXPECT_EQ(teacher->getNewHorizonsLearningMentorRecipients(), (Recipients{recipients[2]->id, ObjectInstanceID::NONE}));
}

TEST_F(NewHorizonsLearningMasterTeacherTest, BasicReceiptSurvivesExpertUpgradeWithoutRepeatOrTopUp)
{
	startGame();
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	const SecondarySkill learning(SecondarySkill::decode(learningId));
	handler.changeSecSkill(teacher, learning, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(select(handler, mentorId));
	const auto firstBefore = recipients[0]->exp;
	const auto basicExpected = recipients[0]->calculateXp(250 * teacher->level);
	handler.heroExchange(teacher->id, recipients[0]->id);
	ASSERT_EQ(recipients[0]->exp - firstBefore, basicExpected);
	handler.changeSecSkill(teacher, learning, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(select(handler, fieldStudyId));
	handler.changeSecSkill(teacher, learning, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(select(handler, teacherId));
	const auto afterBasic = recipients[0]->exp;
	handler.heroExchange(teacher->id, recipients[0]->id);
	EXPECT_EQ(recipients[0]->exp, afterBasic);
	const auto nextBefore = recipients[1]->exp;
	const TExpType upgradedExpected = 0;
	handler.heroExchange(teacher->id, recipients[1]->id);
	EXPECT_EQ(recipients[1]->exp - nextBefore, upgradedExpected) << "Expert Philosopher does not add a second weekly Mentor recipient";
}

TEST_F(NewHorizonsLearningMasterTeacherTest, TownMeetingAndCurrentSaveRetainTheSingleWeeklyReceipt)
{
	startGame();
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	ASSERT_NO_FATAL_FAILURE(prepareExpert(handler));
	town->setGarrisonedHero(teacher);
	const auto before = recipients[0]->exp;
	const auto expected = recipients[0]->calculateXp(250 * teacher->level);
	handler.heroVisitCastle(town, recipients[0]);
	EXPECT_EQ(recipients[0]->exp - before, expected);
	EXPECT_EQ(town->getGarrisonHero(), teacher);
	EXPECT_EQ(town->getVisitingHero(), recipients[0]);
	ASSERT_NO_FATAL_FAILURE(settleCurrentQueries(handler));
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * savedTeacher = restored.getHero(teacher->id);
	ASSERT_NE(savedTeacher, nullptr);
	EXPECT_EQ(savedTeacher->getNewHorizonsLearningMentorRecipients(), teacher->getNewHorizonsLearningMentorRecipients());
	EXPECT_FALSE(savedTeacher->canGrantNewHorizonsLearningMentorTo(*restored.getHero(recipients[0]->id), 0));
	EXPECT_FALSE(savedTeacher->canGrantNewHorizonsLearningMentorTo(*restored.getHero(recipients[1]->id), 0));
}
TEST_F(NewHorizonsLearningMasterTeacherTest, InactiveRankEnemyEqualAndSelfDoNotSpendHistory)
{
	startGame();
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	const auto before = recipients[0]->exp;
	handler.heroExchange(teacher->id, recipients[0]->id);
	EXPECT_EQ(recipients[0]->exp, before);
	ASSERT_NO_FATAL_FAILURE(prepareExpert(handler));
	auto & perkState = const_cast<newHorizonsHeroes::PerkState &>(teacher->getPerkState());
	const auto activeRules = perkState.rules;
	for(auto & perk : perkState.rules["skills"][learningId]["perks"].Vector())
		if(perk["id"].String() == mentorId)
			perk["effect"]["status"].String() = "planned";
	handler.heroExchange(teacher->id, recipients[0]->id);
	EXPECT_EQ(recipients[0]->exp, before);
	perkState.rules = activeRules;
	const SecondarySkill learning(SecondarySkill::decode(learningId));
	handler.changeSecSkill(teacher, learning, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	handler.heroExchange(teacher->id, recipients[0]->id);
	EXPECT_EQ(recipients[0]->exp, before);
	handler.changeSecSkill(teacher, learning, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	recipients[0]->level = teacher->level;
	handler.heroExchange(teacher->id, recipients[0]->id);
	EXPECT_EQ(recipients[0]->exp, before);
	const auto enemyBefore = enemy->exp;
	handler.heroExchange(teacher->id, enemy->id);
	handler.heroExchange(teacher->id, teacher->id);
	EXPECT_EQ(enemy->exp, enemyBefore);
	EXPECT_EQ(teacher->getNewHorizonsLearningMentorLastWeek(), -1);
}

TEST(NewHorizonsLearningMasterTeacherWire, CurrentSingleRecipientHistoryValidatesRoundTripsAndClearsReusedRead)
{
	SetNewHorizonsLearningMentorState outgoing;
	outgoing.heroId = ObjectInstanceID(42);
	outgoing.lastUseWeek = 9;
	outgoing.recipientIds = {ObjectInstanceID(43), ObjectInstanceID::NONE};
	CMemorySerializer wire;
	wire.oser & outgoing;
	SetNewHorizonsLearningMentorState incoming;
	wire.iser & incoming;
	EXPECT_EQ(incoming.recipientIds, outgoing.recipientIds);
	EXPECT_EQ(incoming.lastUseWeek, 9);
	EXPECT_TRUE(CGHeroInstance::isValidNewHorizonsLearningMentorState(outgoing.heroId, 9, outgoing.recipientIds));
	EXPECT_FALSE(CGHeroInstance::isValidNewHorizonsLearningMentorState(outgoing.heroId, 9, {ObjectInstanceID(43), ObjectInstanceID(43)}));
	EXPECT_FALSE(CGHeroInstance::isValidNewHorizonsLearningMentorState(outgoing.heroId, -1, outgoing.recipientIds));
	EXPECT_FALSE(CGHeroInstance::isValidNewHorizonsLearningMentorState(outgoing.heroId, 9, {ObjectInstanceID::NONE, ObjectInstanceID(44)}));
	EXPECT_FALSE(CGHeroInstance::isValidNewHorizonsLearningMentorState(outgoing.heroId, 9, {outgoing.heroId, ObjectInstanceID::NONE}));
	SetNewHorizonsLearningMentorState empty;
	empty.heroId = outgoing.heroId;
	CMemorySerializer fresh;
	fresh.oser & empty;
	fresh.iser & incoming;
	EXPECT_EQ(incoming.lastUseWeek, -1);
	EXPECT_EQ(incoming.recipientIds, (Recipients{ObjectInstanceID::NONE, ObjectInstanceID::NONE}));
	SetNewHorizonsLearningMentorState invalid = outgoing;
	invalid.recipientIds[1] = invalid.recipientIds[0];
	CMemorySerializer invalidWrite;
	EXPECT_THROW(invalidWrite.oser & invalid, std::runtime_error);
	EXPECT_TRUE(invalidWrite.extractBuffer().empty());
	CMemorySerializer invalidRead;
	invalidRead.oser & invalid.heroId;
	invalidRead.oser & invalid.lastUseWeek;
	invalidRead.oser & invalid.recipientIds;
	EXPECT_THROW(invalidRead.iser & incoming, std::runtime_error);
}

TEST_F(NewHorizonsLearningMasterTeacherTest, ActualOrisStartingEagleEyeAndExpertPhilosopherGrantNoFreeMentor)
{
	oris = true;
	startGame();
	ASSERT_TRUE(teacher->getPerkState().hasSelection(learningId, "new-horizons:learning.eagleEye"));
	EXPECT_EQ(teacher->getNewHorizonsLearningMentorExperiencePerLevel(), 0);
	EXPECT_EQ(teacher->calculateXp(1000), 1150);
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	ASSERT_NO_FATAL_FAILURE(prepareExpert(handler, false));
	for(auto * recipient : recipients)
	{
		const auto before = recipient->exp;
		handler.heroExchange(teacher->id, recipient->id);
		EXPECT_EQ(recipient->exp, before);
	}
	EXPECT_EQ(teacher->getNewHorizonsLearningMentorLastWeek(), -1);
	EXPECT_FALSE(teacher->hasActivePerk(learningId, mentorId));
}
TEST_F(NewHorizonsLearningMasterTeacherTest, OrisHistoricalAbsencePreservesOrdinaryAwardAndLearning)
{
	specialtyCaptured = false;
	customOrisMentor = true;
	startGame();
	EXPECT_EQ(teacher->calculateXp(1000), 1100);
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	ASSERT_NO_FATAL_FAILURE(prepareExpert(handler));
	const auto before = recipients[0]->exp;
	const auto expected = recipients[0]->calculateXp(250 * teacher->level);
	handler.heroExchange(teacher->id, recipients[0]->id);
	EXPECT_EQ(recipients[0]->exp - before, expected);
}

TEST_F(NewHorizonsLearningMasterTeacherTest, OrisRevalidatesSeparateRawAwardBeforeSpendingWeeklyReceipt)
{
	customOrisMentor = true;
	startGame();
	OrisExchangeMutationServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	ASSERT_NO_FATAL_FAILURE(prepareExpert(handler));
	const auto before = recipients[0]->exp;
	auto & captured = const_cast<JsonNode &>(teacher->getPrimaryGrowthRules());
	const auto original = captured;
	// Adversarial in-process policy change between preparation and grant.
	server.onExchange = [&captured]()
	{
		captured["additionalSpecialties"]["spiritTeacher"]["mentorExperiencePercent"].Integer() = 26;
	};
	handler.heroExchange(teacher->id, recipients[0]->id);
	captured = original;
	server.onExchange = {};
	EXPECT_EQ(recipients[0]->exp, before);
	EXPECT_EQ(teacher->getNewHorizonsLearningMentorRecipients(),
		(Recipients{ObjectInstanceID::NONE, ObjectInstanceID::NONE}));
}

TEST_F(NewHorizonsLearningMasterTeacherTest, CustomOrisMentorUsesCurrent250PerLevelAndTheRetainedRawSpecialty)
{
	customOrisMentor = true;
	startGame();
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	ASSERT_NO_FATAL_FAILURE(prepareExpert(handler));
	const auto raw = 250 * 10 + (250 * 10 / 4);
	ASSERT_EQ(raw, 3125);
	const auto before = recipients[0]->exp;
	const auto expected = recipients[0]->calculateXp(raw);
	handler.heroExchange(teacher->id, recipients[0]->id);
	EXPECT_EQ(recipients[0]->exp - before, expected);
	const auto otherBefore = recipients[1]->exp;
	handler.heroExchange(teacher->id, recipients[1]->id);
	EXPECT_EQ(recipients[1]->exp, otherBefore);
	EXPECT_EQ(teacher->getNewHorizonsLearningMentorRecipients(), (Recipients{recipients[0]->id, ObjectInstanceID::NONE}));
}
