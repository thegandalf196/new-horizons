/*
 * NewHorizonsNecromancyAmplifierTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../battles/BattleTestFixture.h"
#include "../../mock/GameHandlerTestServer.h"
#include "../../mock/TinyH3MBuilder.h"
#include "../../mock/TinyMapGameTest.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusEnum.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/entities/hero/NewHorizonsNecromancy.h"
#include "../../../lib/filesystem/ResourcePath.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../server/queries/BattleQueries.h"
#include "../../../server/queries/QueriesProcessor.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace
{
CreatureID creature(const char * id)
{
	return CreatureID(CreatureID::decode(id));
}

void configureNewHorizons(CMap * loaded)
{
	loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
		JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
	loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
		JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
		JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
}

std::vector<int32_t> amplifierDurations(const CGHeroInstance & hero)
{
	std::vector<int32_t> result;
	for(const auto & bonus : hero.getExportedBonusList())
	{
		if(!bonus
			|| bonus->type != BonusType::UNDEAD_RAISE_PERCENTAGE
			|| bonus->source != BonusSource::TOWN_STRUCTURE
			|| bonus->stacking != newHorizonsNecromancy::AMPLIFIER_STACKING_KEY
			|| !Bonus::NDays(bonus.get()))
			continue;

		result.push_back(bonus->turnsRemain);
	}
	std::ranges::sort(result);
	return result;
}

class NewHorizonsNecromancyAmplifierVisitTest : public TinyMapGameTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		configureNewHorizons(loaded);
	}

	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}
};

/// The battle fixture keeps the Necromancer class in the saved hero profile and
/// makes player 0 a computer player, then uses the ordinary server town-visit
/// and battle-result paths.
class NewHorizonsNecromancyAmplifierBattleTest : public BattleTestFixture
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		configureNewHorizons(loaded);
	}

	void configurePlayer(PlayerSettings & settings) const override
	{
		BattleTestFixture::configurePlayer(settings);
		if(settings.color == PlayerColor(0))
			settings.connectedPlayerIDs.clear();
	}

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";

		const CreatureID token(0);
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NecromancyAmplifierBattle")
			.playerActive(PlayerColor(0))
			.playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(72), PlayerColor(0)).heroGarrison({{token, 1}})
			.hero({7, 7, 0}, HeroTypeID(1), PlayerColor(1)).heroGarrison({{token, 1}})
			.town({10, 10, 0}, FactionID::NECROPOLIS, PlayerColor(0)).townGarrison({});
		startWithMap(std::move(builder));

		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		makeNeutral(attackerSideHero);
		makeNeutral(defenderSideHero);
		gameHandler->onAdvInterfaceReady(PlayerColor(0));
		gameHandler->onAdvInterfaceReady(PlayerColor(1));
	}

	void makeNeutral(CGHeroInstance * hero)
	{
		for(const auto & bonus : hero->getHeroType()->specialty)
			hero->removeBonus(bonus);
		for(int i = 0; i < LIBRARY->skillh->size(); ++i)
			hero->setSecSkillLevel(SecondarySkill(i), 0, ChangeValueMode::ABSOLUTE);
		for(auto skill : {PrimarySkill::ATTACK, PrimarySkill::DEFENSE,
			PrimarySkill::SPELL_POWER, PrimarySkill::KNOWLEDGE})
			hero->setPrimarySkill(skill, 0, ChangeValueMode::ABSOLUTE);
	}

	int32_t armyCreatureCount(const CGHeroInstance & hero, CreatureID creatureId) const
	{
		int32_t total = 0;
		for(const auto & [slot, stack] : hero.Slots())
		{
			(void)slot;
			if(stack->getCreatureID() == creatureId)
				total += stack->getCount();
		}
		return total;
	}

	void resolveBattleDialogsAndProgression()
	{
		for(const auto player : {PlayerColor(0), PlayerColor(1)})
		{
			auto dialog = gameHandler->queries->topQuery(player);
			if(dialog && dialog->getType() == QueryType::BattleDialog)
			{
				ASSERT_TRUE(gameHandler->queryReply(dialog->queryID, 0, player));
			}
		}
		for(int remainingLevelUps = 10; remainingLevelUps > 0; --remainingLevelUps)
		{
			auto followup = gameHandler->queries->topQuery(PlayerColor(0));
			if(!followup)
				break;
			ASSERT_EQ(followup->getType(), QueryType::HeroLevelUpDialog);
			ASSERT_TRUE(gameHandler->queryReply(followup->queryID, 0, PlayerColor(0)));
		}
		EXPECT_EQ(gameHandler->queries->topQuery(PlayerColor(0)), nullptr);
	}
};
}

TEST(NewHorizonsNecromancyAmplifier, ResolverAddsPercentagePointsBeforeExactCasualtyFloor)
{
	const auto ordinary = newHorizonsNecromancy::resolve(1, 100, 0, false, false, false,
		true, true, 0, 0, 0, false, true, CreatureID::NONE, {}, {}, false, false, true, 0);
	const auto amplified = newHorizonsNecromancy::resolve(1, 100, 0, false, false, false,
		true, true, 0, 0, 0, false, true, CreatureID::NONE, {}, {}, false, false, true, 10);

	EXPECT_TRUE(ordinary.active);
	EXPECT_EQ(ordinary.eligibleCasualties, 100);
	EXPECT_EQ(ordinary.percentage, 10);
	EXPECT_EQ(ordinary.skeletonsOffered, 10);
	EXPECT_EQ(ordinary.skeletonsRaised, 10);
	EXPECT_TRUE(amplified.active);
	EXPECT_EQ(amplified.eligibleCasualties, 100);
	EXPECT_EQ(amplified.percentage, 20);
	EXPECT_EQ(amplified.skeletonsOffered, 20);
	EXPECT_EQ(amplified.skeletonsRaised, 20);
}

TEST_F(NewHorizonsNecromancyAmplifierVisitTest, RealBuildingVisitIsHeroScopedRefreshesAndExpiresAfterSevenDays)
{
	const CreatureID token(0);
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("NecromancyAmplifierVisit")
		.playerActive(PlayerColor(0))
		.playerActive(PlayerColor(1))
		.hero({5, 5, 0}, HeroTypeID(72), PlayerColor(0)).heroGarrison({{token, 1}})
		.hero({7, 5, 0}, HeroTypeID(73), PlayerColor(0)).heroGarrison({{token, 1}})
		.hero({9, 5, 0}, HeroTypeID(0), PlayerColor(0)).heroGarrison({{token, 1}})
		.town({15, 15, 0}, FactionID::NECROPOLIS, PlayerColor(0)).townGarrison({})
		.town({25, 15, 0}, FactionID::NECROPOLIS, PlayerColor(0)).townGarrison({});
	startWithMap(std::move(builder));

	auto * visitor = findHeroAt({5, 5, 0});
	auto * remoteNecromancer = findHeroAt({7, 5, 0});
	auto * ordinaryHero = findHeroAt({9, 5, 0});
	ASSERT_NE(visitor, nullptr);
	ASSERT_NE(remoteNecromancer, nullptr);
	ASSERT_NE(ordinaryHero, nullptr);
	const auto towns = findAll<CGTownInstance>();
	ASSERT_EQ(towns.size(), 2u);
	auto * firstAmplifier = towns[0];
	auto * secondAmplifier = towns[1];
	ASSERT_NE(firstAmplifier, nullptr);
	ASSERT_NE(secondAmplifier, nullptr);
	ASSERT_TRUE(firstAmplifier->rewardableBuildings.contains(BuildingID::SPECIAL_2));
	ASSERT_TRUE(secondAmplifier->rewardableBuildings.contains(BuildingID::SPECIAL_2));
	ASSERT_FALSE(firstAmplifier->hasBuilt(BuildingID::SPECIAL_2));
	ASSERT_FALSE(secondAmplifier->hasBuilt(BuildingID::SPECIAL_2));

	firstAmplifier->addBuilding(BuildingID::SPECIAL_2);
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler gameHandler(server, gameState());
	gameHandler.randomizer->setSeed(104);
	const int necromancyIndex = SecondarySkill::decode(newHorizonsNecromancy::SKILL_ID);
	ASSERT_GE(necromancyIndex, 0);
	const SecondarySkill necromancy(necromancyIndex);
	if(visitor->getSecSkillLevel(necromancy) != MasteryLevel::BASIC)
	{
		// This canonical Death Knight starts with Necromancy already; control only
		// the rank under test, then obtain Basic through the ordinary server path.
		visitor->setSecSkillLevel(necromancy, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
		gameHandler.levelUpHero(visitor, necromancy, false);
	}
	ASSERT_EQ(visitor->getSecSkillLevel(necromancy), MasteryLevel::BASIC);
	ASSERT_TRUE(visitor->usesNewHorizonsNecromancy());
	const int32_t visitorRaiseBaseline = visitor->valOfBonuses(BonusType::UNDEAD_RAISE_PERCENTAGE);
	const int32_t remoteRaiseBaseline = remoteNecromancer->valOfBonuses(BonusType::UNDEAD_RAISE_PERCENTAGE);
	const int32_t ordinaryRaiseBaseline = ordinaryHero->valOfBonuses(BonusType::UNDEAD_RAISE_PERCENTAGE);

	firstAmplifier->setVisitingHero(visitor);
	gameHandler.heroVisitCastle(firstAmplifier, visitor);
	EXPECT_EQ(visitor->getNewHorizonsNecromancyAmplifierBonusPercent(), 10);
	EXPECT_EQ(visitor->valOfBonuses(BonusType::UNDEAD_RAISE_PERCENTAGE), visitorRaiseBaseline + 10);
	EXPECT_EQ(remoteNecromancer->getNewHorizonsNecromancyAmplifierBonusPercent(), 0);
	EXPECT_EQ(remoteNecromancer->valOfBonuses(BonusType::UNDEAD_RAISE_PERCENTAGE), remoteRaiseBaseline);
	EXPECT_EQ(ordinaryHero->getNewHorizonsNecromancyAmplifierBonusPercent(), 0);
	firstAmplifier->setVisitingHero(nullptr);

	// The second configured building is unbuilt, so visiting it cannot award the effect.
	secondAmplifier->setVisitingHero(remoteNecromancer);
	gameHandler.heroVisitCastle(secondAmplifier, remoteNecromancer);
	EXPECT_EQ(remoteNecromancer->getNewHorizonsNecromancyAmplifierBonusPercent(), 0);
	secondAmplifier->setVisitingHero(nullptr);

	// The ordinary Hero class is not in the reward limiter, even at a built Amplifier.
	firstAmplifier->setVisitingHero(ordinaryHero);
	gameHandler.heroVisitCastle(firstAmplifier, ordinaryHero);
	EXPECT_EQ(ordinaryHero->getNewHorizonsNecromancyAmplifierBonusPercent(), 0);
	EXPECT_EQ(ordinaryHero->valOfBonuses(BonusType::UNDEAD_RAISE_PERCENTAGE), ordinaryRaiseBaseline);
	firstAmplifier->setVisitingHero(nullptr);
	EXPECT_EQ(visitor->getNewHorizonsNecromancyAmplifierBonusPercent(), 10);

	const auto visitDay = gameState()->day;
	for(int day = 0; day < 6; ++day)
		gameHandler.onNewTurn();
	EXPECT_EQ(gameState()->day, visitDay + 6);
	EXPECT_EQ(visitor->getNewHorizonsNecromancyAmplifierBonusPercent(), 10);
	const auto beforeRefreshDurations = amplifierDurations(*visitor);
	ASSERT_EQ(beforeRefreshDurations.size(), 1u);
	EXPECT_EQ(beforeRefreshDurations.front(), 1);

	// A second physical building grants a second same-key timed entry: it does not
	// stack numerically, and it keeps the effect alive beyond the first entry's expiry.
	secondAmplifier->addBuilding(BuildingID::SPECIAL_2);
	secondAmplifier->setVisitingHero(visitor);
	gameHandler.heroVisitCastle(secondAmplifier, visitor);
	EXPECT_EQ(visitor->getNewHorizonsNecromancyAmplifierBonusPercent(), 10);
	EXPECT_EQ(visitor->valOfBonuses(BonusType::UNDEAD_RAISE_PERCENTAGE), visitorRaiseBaseline + 10);
	const auto refreshedDurations = amplifierDurations(*visitor);
	ASSERT_EQ(refreshedDurations.size(), 2u);
	EXPECT_EQ(refreshedDurations[0], 1);
	EXPECT_EQ(refreshedDurations[1], 7);
	secondAmplifier->setVisitingHero(nullptr);

	const auto visitorID = visitor->id;
	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredVisitor = restored.getHero(visitorID);
	ASSERT_NE(restoredVisitor, nullptr);
	EXPECT_EQ(restoredVisitor->getNewHorizonsNecromancyAmplifierBonusPercent(), 10);
	EXPECT_EQ(amplifierDurations(*restoredVisitor), refreshedDurations);

	for(int day = 0; day < 6; ++day)
		gameHandler.onNewTurn();
	EXPECT_EQ(visitor->getNewHorizonsNecromancyAmplifierBonusPercent(), 10);
	const auto afterOldBonusExpiry = amplifierDurations(*visitor);
	ASSERT_EQ(afterOldBonusExpiry.size(), 1u);
	EXPECT_EQ(afterOldBonusExpiry.front(), 1);
	gameHandler.onNewTurn();
	EXPECT_EQ(visitor->getNewHorizonsNecromancyAmplifierBonusPercent(), 0);
	EXPECT_EQ(visitor->valOfBonuses(BonusType::UNDEAD_RAISE_PERCENTAGE), visitorRaiseBaseline);
	EXPECT_TRUE(amplifierDurations(*visitor).empty());
}

TEST_F(NewHorizonsNecromancyAmplifierBattleTest, ComputerWinnerUsesRealVisitedAmplifierInPostBattleRaising)
{
	const int necromancyIndex = SecondarySkill::decode(newHorizonsNecromancy::SKILL_ID);
	ASSERT_GE(necromancyIndex, 0);
	gameHandler->levelUpHero(attackerSideHero, SecondarySkill(necromancyIndex), false);
	ASSERT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill(necromancyIndex)), MasteryLevel::BASIC);
	ASSERT_TRUE(attackerSideHero->usesNewHorizonsNecromancy());

	const auto towns = findAll<CGTownInstance>();
	ASSERT_EQ(towns.size(), 1u);
	auto * amplifier = towns.front();
	ASSERT_NE(amplifier, nullptr);
	ASSERT_TRUE(amplifier->rewardableBuildings.contains(BuildingID::SPECIAL_2));
	amplifier->addBuilding(BuildingID::SPECIAL_2);
	amplifier->setVisitingHero(attackerSideHero);
	gameHandler->heroVisitCastle(amplifier, attackerSideHero);
	EXPECT_EQ(attackerSideHero->getNewHorizonsNecromancyAmplifierBonusPercent(), 10);
	amplifier->setVisitingHero(nullptr);

	ASSERT_TRUE(attackerSideHero->usesNewHorizonsNecromancy());
	EXPECT_FALSE(attackerSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::DARK_CONVERSION_ID));

	const auto skeleton = creature("core:skeleton");
	const auto pikeman = creature("core:pikeman");
	attackerSideHero->clearSlots();
	defenderSideHero->clearSlots();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), skeleton, 1));
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), pikeman, 50));

	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_NE(gameState()->getBattle(PlayerColor(0)), nullptr);
	gameHandler->battles->cheatBattleVictory(PlayerColor(0));
	resolveBattleDialogsAndProgression();

	EXPECT_EQ(gameState()->getBattle(PlayerColor(0)), nullptr);
	EXPECT_EQ(armyCreatureCount(*attackerSideHero, skeleton), 11);
	EXPECT_EQ(attackerSideHero->getNewHorizonsNecromancyAmplifierBonusPercent(), 10);
}
