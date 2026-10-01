/*
 * NewHorizonsLearningMentorTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"

#include <gtest/gtest.h>

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
#include "../../server/queries/QueriesProcessor.h"
#include "../../server/queries/MapQueries.h"
#include "../mock/GameHandlerTestServer.h"
#include "../mock/TinyH3MBuilder.h"
#include "../mock/TinyMapGameTest.h"

namespace
{
constexpr auto LEARNING_SKILL = "new-horizons:learning";
constexpr auto MENTOR_PERK = "new-horizons:learning.mentor";

class NewHorizonsLearningMentorTest : public TinyMapGameTest
{
protected:
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false)
			.playerActive(PlayerColor(0))
			.playerActive(PlayerColor(1))
			.town({12, 12, 0}, FactionID(FactionID::decode("core:castle")), PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
			.hero({6, 6, 0}, HeroTypeID(1), PlayerColor(0))
			.hero({7, 7, 0}, HeroTypeID(2), PlayerColor(0))
			.hero({8, 8, 0}, HeroTypeID(3), PlayerColor(1));
		startWithMap(std::move(builder));

		town = findFirst<CGTownInstance>();
		ASSERT_NE(town, nullptr);
		for(auto * hero : findAll<CGHeroInstance>())
		{
			if(hero->getOwner() == PlayerColor(0))
				alliedHeroes.push_back(hero);
			else if(hero->getOwner() == PlayerColor(1))
				enemyHero = hero;
		}
		ASSERT_EQ(alliedHeroes.size(), 3u);
		ASSERT_NE(enemyHero, nullptr);
	}

	static SecondarySkill learningSkill()
	{
		return SecondarySkill(SecondarySkill::decode(LEARNING_SKILL));
	}

	void setLearningRank(CGHeroInstance * hero, int rank)
	{
		const auto skill = learningSkill();
		ASSERT_GE(skill.getNum(), 0);
		hero->setSecSkillLevel(skill, rank, ChangeValueMode::ABSOLUTE);
	}

	void enableMentor(CGHeroInstance * hero)
	{
		setLearningRank(hero, MasteryLevel::BASIC);
		hero->applyPerkSelection({LEARNING_SKILL, MENTOR_PERK});
	}

	CGTownInstance * town = nullptr;
	std::vector<CGHeroInstance *> alliedHeroes;
	CGHeroInstance * enemyHero = nullptr;
};
}

TEST_F(NewHorizonsLearningMentorTest, FieldMeetingAwardsOncePerWeekAndLevelUpSitsAboveExchange)
{
	startGame();
	auto * mentor = alliedHeroes[0];
	auto * firstRecipient = alliedHeroes[1];
	auto * secondRecipient = alliedHeroes[2];
	mentor->level = 8;
	enableMentor(mentor);
	setLearningRank(firstRecipient, MasteryLevel::BASIC);

	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler gameHandler(server, gameState());
	const auto firstBefore = firstRecipient->exp;
	const auto firstExpected = firstRecipient->calculateXp(250 * mentor->level);
	gameHandler.heroExchange(mentor->id, firstRecipient->id);

	EXPECT_EQ(firstRecipient->exp - firstBefore, firstExpected);
	EXPECT_EQ(firstRecipient->exp - firstBefore, 2200);
	EXPECT_EQ(mentor->getNewHorizonsLearningMentorLastWeek(), 0);

	const auto levelUp = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(
		gameHandler.queries->topQuery(PlayerColor(0)));
	ASSERT_NE(levelUp, nullptr);
	bool exchangeRemainsUnderLevelUp = false;
	for(const auto & query : gameHandler.queries->allQueries())
	{
		const auto exchange = std::dynamic_pointer_cast<CGarrisonDialogQuery>(query);
		if(exchange && exchange->exchangingArmies[0] == mentor && exchange->exchangingArmies[1] == firstRecipient)
			exchangeRemainsUnderLevelUp = true;
	}
	EXPECT_TRUE(exchangeRemainsUnderLevelUp);

	const auto firstAfterAward = firstRecipient->exp;
	const auto secondBefore = secondRecipient->exp;
	gameHandler.heroExchange(mentor->id, firstRecipient->id);
	gameHandler.heroExchange(mentor->id, secondRecipient->id);
	EXPECT_EQ(firstRecipient->exp, firstAfterAward);
	EXPECT_EQ(secondRecipient->exp, secondBefore);
	EXPECT_EQ(mentor->getNewHorizonsLearningMentorLastWeek(), 0);

	gameState()->day = 8;
	const auto nextWeekExpected = secondRecipient->calculateXp(250 * mentor->level);
	gameHandler.heroExchange(mentor->id, secondRecipient->id);
	EXPECT_EQ(secondRecipient->exp - secondBefore, nextWeekExpected);
	EXPECT_EQ(mentor->getNewHorizonsLearningMentorLastWeek(), 1);

	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredMentor = restored.getHero(mentor->id);
	ASSERT_NE(restoredMentor, nullptr);
	EXPECT_EQ(restoredMentor->getNewHorizonsLearningMentorLastWeek(), 1);
}

TEST_F(NewHorizonsLearningMentorTest, OnlyActiveBasicMentorAndStrictlyHigherAlliedHeroesQualify)
{
	startGame();
	auto * mentor = alliedHeroes[0];
	auto * recipient = alliedHeroes[1];
	mentor->level = 5;
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler gameHandler(server, gameState());

	// No skill/perk, a missing rank, equal level, a planned effect, self, and an
	// enemy encounter must all leave both Experience and the weekly marker alone.
	const auto recipientStart = recipient->exp;
	const auto enemyStart = enemyHero->exp;
	gameHandler.heroExchange(mentor->id, recipient->id);
	EXPECT_EQ(recipient->exp, recipientStart);
	EXPECT_EQ(mentor->getNewHorizonsLearningMentorLastWeek(), -1);

	setLearningRank(mentor, MasteryLevel::BASIC);
	gameHandler.heroExchange(mentor->id, recipient->id);
	EXPECT_EQ(recipient->exp, recipientStart);
	EXPECT_EQ(mentor->getNewHorizonsLearningMentorLastWeek(), -1);

	mentor->applyPerkSelection({LEARNING_SKILL, MENTOR_PERK});
	mentor->setSecSkillLevel(learningSkill(), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	gameHandler.heroExchange(mentor->id, recipient->id);
	EXPECT_EQ(recipient->exp, recipientStart);
	EXPECT_EQ(mentor->getNewHorizonsLearningMentorLastWeek(), -1);

	mentor->setSecSkillLevel(learningSkill(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(mentor->hasActivePerk(LEARNING_SKILL, MENTOR_PERK));
	mentor->level = recipient->level;
	gameHandler.heroExchange(mentor->id, recipient->id);
	EXPECT_EQ(recipient->exp, recipientStart);
	EXPECT_EQ(mentor->getNewHorizonsLearningMentorLastWeek(), -1);
	mentor->level = 5;

	auto & perkState = const_cast<newHorizonsHeroes::PerkState &>(mentor->getPerkState());
	auto plannedRules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	for(auto & perk : plannedRules["skills"][LEARNING_SKILL]["perks"].Vector())
	{
		if(perk["id"].String() == MENTOR_PERK)
			perk["effect"]["status"].String() = "planned";
	}
	perkState.rules = std::move(plannedRules);
	EXPECT_FALSE(mentor->hasActivePerk(LEARNING_SKILL, MENTOR_PERK));
	gameHandler.heroExchange(mentor->id, recipient->id);
	EXPECT_EQ(recipient->exp, recipientStart);
	EXPECT_EQ(mentor->getNewHorizonsLearningMentorLastWeek(), -1);
	perkState.rules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));

	gameHandler.heroExchange(mentor->id, mentor->id);
	gameHandler.heroExchange(mentor->id, enemyHero->id);
	EXPECT_EQ(recipient->exp, recipientStart);
	EXPECT_EQ(enemyHero->exp, enemyStart);
	EXPECT_EQ(mentor->getNewHorizonsLearningMentorLastWeek(), -1);

	gameHandler.heroExchange(mentor->id, recipient->id);
	EXPECT_GT(recipient->exp, recipientStart);
	EXPECT_EQ(mentor->getNewHorizonsLearningMentorLastWeek(), 0);
}

TEST_F(NewHorizonsLearningMentorTest, TownVisitIsAMeetingAndPersistsTheAuthoritativeMarker)
{
	startGame();
	auto * mentor = alliedHeroes[0];
	auto * recipient = alliedHeroes[1];
	mentor->level = 4;
	enableMentor(mentor);
	town->setGarrisonedHero(mentor);

	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler gameHandler(server, gameState());
	const auto before = recipient->exp;
	const auto expected = recipient->calculateXp(250 * mentor->level);
	gameHandler.heroVisitCastle(town, recipient);

	EXPECT_EQ(town->getVisitingHero(), recipient);
	EXPECT_EQ(town->getGarrisonHero(), mentor);
	EXPECT_EQ(recipient->exp - before, expected);
	EXPECT_EQ(mentor->getNewHorizonsLearningMentorLastWeek(), 0);

	const auto afterMeeting = recipient->exp;
	gameHandler.heroVisitCastle(town, recipient);
	EXPECT_EQ(recipient->exp, afterMeeting);

	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredMentor = restored.getHero(mentor->id);
	ASSERT_NE(restoredMentor, nullptr);
	EXPECT_EQ(restoredMentor->getNewHorizonsLearningMentorLastWeek(), 0);
}

TEST(NewHorizonsLearningMentorWire, StateRoundTripsAndOldFormatRejectsTheNewPacket)
{
	SetNewHorizonsLearningMentorState outgoing;
	outgoing.heroId = ObjectInstanceID(42);
	outgoing.lastUseWeek = 9;

	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::CURRENT;
	wire.iser.version = ESerializationVersion::CURRENT;
	wire.oser & outgoing;

	SetNewHorizonsLearningMentorState incoming;
	wire.iser & incoming;
	EXPECT_EQ(incoming.heroId, outgoing.heroId);
	EXPECT_EQ(incoming.lastUseWeek, outgoing.lastUseWeek);

	CMemorySerializer legacyWire;
	legacyWire.oser.version = ESerializationVersion::NEW_HORIZONS_MUSTER;
	SetNewHorizonsLearningMentorState legacyOutgoing;
	EXPECT_THROW(legacyWire.oser & legacyOutgoing, std::runtime_error);
}
