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
#include "../../server/CGameHandler.h"
#include "../../server/queries/MapQueries.h"
#include "../../server/queries/QueriesProcessor.h"
#include "battles/FullGameSnapshotTypes.h"
#include "../mock/GameHandlerTestServer.h"
#include "../mock/TinyMapGameTest.h"

namespace
{
constexpr auto learningId = "new-horizons:learning";
constexpr auto teacherId = "new-horizons:learning.masterTeacher";
constexpr auto mentorId = "new-horizons:learning.mentor";
constexpr auto historianId = "new-horizons:learning.historian";
constexpr auto fieldStudyId = "new-horizons:learning.fieldStudy";
using Recipients = CGHeroInstance::LearningMentorRecipients;

class NewHorizonsLearningMasterTeacherTest : public TinyMapGameTest
{
protected:
	CGHeroInstance * teacher = nullptr;
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
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
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
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
			.hero({6, 6, 0}, HeroTypeID(1), PlayerColor(0))
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
			const auto offers = teacher->getPerkState().prepareOffer(rankLookup, seed);
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

	void prepareExpert(CGameHandler & handler, bool withMentor = false)
	{
		if(!withMentor)
		{
			// Production currently has no active non-Mentor Basic Learning choice.
			// This captured test catalog enables Historian only to prove independent
			// tier eligibility; it does not exercise or certify Historian's mechanic.
			auto & state = const_cast<newHorizonsHeroes::PerkState &>(teacher->getPerkState());
			for(auto & perk : state.rules["skills"][learningId]["perks"].Vector())
				if(perk["id"].String() == historianId)
					perk["effect"]["status"].String() = "active";
			state.validate();
			RecordProperty("standalone_prerequisite", "captured-test-only Historian Basic tier");
		}
		const SecondarySkill learning(SecondarySkill::decode(learningId));
		ASSERT_GE(learning.getNum(), 0);
		handler.changeSecSkill(teacher, learning, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(select(handler, withMentor ? mentorId : historianId));
		handler.changeSecSkill(teacher, learning, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(select(handler, fieldStudyId));
		handler.changeSecSkill(teacher, learning, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(select(handler, teacherId));
		ASSERT_EQ(teacher->getNewHorizonsLearningMentorExperiencePerLevel(), 500);
	}
};
}

TEST_F(NewHorizonsLearningMasterTeacherTest, StandaloneExpertAwardsTwoDistinctRecipientsAndRenewsNextWeek)
{
	startGame();
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	ASSERT_NO_FATAL_FAILURE(prepareExpert(handler));
	ASSERT_FALSE(teacher->hasActivePerk(learningId, mentorId));
	handler.changeSecSkill(recipients[0], SecondarySkill(SecondarySkill::decode(learningId)),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	const auto firstBefore = recipients[0]->exp;
	const auto expected = recipients[0]->calculateXp(500 * teacher->level);
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
	const auto secondExpected = recipients[1]->calculateXp(500 * teacher->level);
	handler.heroExchange(recipients[1]->id, teacher->id);
	EXPECT_EQ(recipients[1]->exp - secondBefore, secondExpected);
	EXPECT_EQ(teacher->getNewHorizonsLearningMentorRecipients(), (Recipients{recipients[0]->id, recipients[1]->id}));
	const auto thirdBefore = recipients[2]->exp;
	handler.heroExchange(teacher->id, recipients[2]->id);
	EXPECT_EQ(recipients[2]->exp, thirdBefore);
	gameState()->day = 8;
	const auto nextExpected = recipients[2]->calculateXp(500 * teacher->level);
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
	const auto upgradedExpected = recipients[1]->calculateXp(500 * teacher->level);
	handler.heroExchange(teacher->id, recipients[1]->id);
	EXPECT_EQ(recipients[1]->exp - nextBefore, upgradedExpected) << "500 replaces 250; grants are not added";
}

TEST_F(NewHorizonsLearningMasterTeacherTest, TownMeetingAndSaveRetainDistinctHistoryAndLegacyUnknownRemainsSpent)
{
	startGame();
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	ASSERT_NO_FATAL_FAILURE(prepareExpert(handler));
	town->setGarrisonedHero(teacher);
	const auto before = recipients[0]->exp;
	const auto expected = recipients[0]->calculateXp(500 * teacher->level);
	handler.heroVisitCastle(town, recipients[0]);
	EXPECT_EQ(recipients[0]->exp - before, expected);
	EXPECT_EQ(town->getGarrisonHero(), teacher);
	EXPECT_EQ(town->getVisitingHero(), recipients[0]);
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * savedTeacher = restored.getHero(teacher->id);
	ASSERT_NE(savedTeacher, nullptr);
	EXPECT_EQ(savedTeacher->getNewHorizonsLearningMentorRecipients(), teacher->getNewHorizonsLearningMentorRecipients());
	EXPECT_FALSE(savedTeacher->canGrantNewHorizonsLearningMentorTo(*restored.getHero(recipients[0]->id), 0));
	EXPECT_TRUE(savedTeacher->canGrantNewHorizonsLearningMentorTo(*restored.getHero(recipients[1]->id), 0));
	CMemorySerializer lossyHero;
	lossyHero.oser.version = ESerializationVersion::NEW_HORIZONS_LUCK_SERENDIPITY;
	EXPECT_THROW(lossyHero.oser & *teacher, std::runtime_error);
	EXPECT_TRUE(lossyHero.extractBuffer().empty());
	teacher->markNewHorizonsLearningMentorUsed(0);
	CGameState resaved;
	resaved.preInit(LIBRARY);
	resaved.loadFromMemory(gameState()->saveToMemory());
	const auto * unknown = resaved.getHero(teacher->id);
	ASSERT_NE(unknown, nullptr);
	EXPECT_EQ(unknown->getNewHorizonsLearningMentorRecipients(), (Recipients{ObjectInstanceID::NONE, ObjectInstanceID::NONE}));
	EXPECT_FALSE(unknown->canGrantNewHorizonsLearningMentorTo(*resaved.getHero(recipients[1]->id), 0));
	EXPECT_TRUE(unknown->canGrantNewHorizonsLearningMentorTo(*resaved.getHero(recipients[1]->id), 1));
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
		if(perk["id"].String() == teacherId)
			perk["effect"]["status"].String() = "planned";
	handler.heroExchange(teacher->id, recipients[0]->id);
	EXPECT_EQ(recipients[0]->exp, before);
	perkState.rules = activeRules;
	const SecondarySkill learning(SecondarySkill::decode(learningId));
	handler.changeSecSkill(teacher, learning, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
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

TEST(NewHorizonsLearningMasterTeacherWire, TypedHistoryValidatesRoundTripsClearsReusedReadAndRejectsLossyWriter)
{
	SetNewHorizonsLearningMentorState outgoing;
	outgoing.heroId = ObjectInstanceID(42);
	outgoing.lastUseWeek = 9;
	outgoing.recipientIds = {ObjectInstanceID(43), ObjectInstanceID(44)};
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
	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::NEW_HORIZONS_LEARNING_MENTOR;
	EXPECT_THROW(legacy.oser & outgoing, std::runtime_error);
	EXPECT_TRUE(legacy.extractBuffer().empty());
	SetNewHorizonsLearningMentorState empty;
	empty.heroId = outgoing.heroId;
	CMemorySerializer fresh;
	fresh.oser & empty;
	fresh.iser & incoming;
	EXPECT_EQ(incoming.lastUseWeek, -1);
	EXPECT_EQ(incoming.recipientIds, (Recipients{ObjectInstanceID::NONE, ObjectInstanceID::NONE}));
	// Reading an older packet into a previously populated object must clear IDs,
	// but retain its conservative used-week marker.
	empty.lastUseWeek = 9;
	CMemorySerializer oldRead;
	oldRead.oser.version = ESerializationVersion::NEW_HORIZONS_LEARNING_MENTOR;
	oldRead.iser.version = ESerializationVersion::NEW_HORIZONS_LEARNING_MENTOR;
	oldRead.oser & empty;
	incoming.recipientIds = outgoing.recipientIds;
	oldRead.iser & incoming;
	EXPECT_EQ(incoming.lastUseWeek, 9);
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
