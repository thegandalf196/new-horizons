/*
 * NewHorizonsBrimstoneSiegeTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file in main folder
 *
 */
#include "StdInc.h"

#include "BattleTestFixture.h"
#include "mock/TinyH3MBuilder.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusEnum.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/battle/BattleLayout.h"
#include "../../../lib/entities/building/CBuilding.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/modding/IdentifierStorage.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../server/CGameHandler.h"

#include <vector>

namespace
{
constexpr int3 ATTACKER_HERO_POS{5, 5, 0};
constexpr int3 DEFENDER_HERO_POS{9, 9, 0};
constexpr int3 INFERNO_TOWN_POS{18, 27, 0};

BonusSourceID brimstoneSource(const CGTownInstance & town)
{
	const auto & building = town.getTown()->buildings.at(BuildingID::SPECIAL_2);
	return BonusSourceID(building->getUniqueTypeID());
}

std::vector<const Bonus *> brimstoneSpellPowerBonuses(const CGHeroInstance & hero, const BonusSourceID & source)
{
	std::vector<const Bonus *> result;
	const auto bonuses = hero.getBonuses(Selector::source(BonusSource::TOWN_STRUCTURE, source)
		.And(Selector::type()(BonusType::PRIMARY_SKILL)));
	for(const auto & bonus : *bonuses)
	{
		if(bonus && bonus->subtype == BonusSubtypeID(PrimarySkill::SPELL_POWER))
			result.push_back(bonus.get());
	}
	return result;
}

class NewHorizonsBrimstoneSiegeTest : public BattleTestFixture
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
	}

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void configurePlayer(PlayerSettings & settings) const override
	{
		BattleTestFixture::configurePlayer(settings);
		if(settings.color == PlayerColor(0))
			settings.connectedPlayerIDs.clear();
	}

	void startScenario()
	{
		const CreatureID archer(CreatureID::decode("core:archer"));
		const CreatureID pikeman(CreatureID::decode("core:pikeman"));
		ASSERT_GE(archer.getNum(), 0);
		ASSERT_GE(pikeman.getNum(), 0);

		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NewHorizonsBrimstoneSiege")
			.playerActive(PlayerColor(0))
			.playerActive(PlayerColor(1))
			.hero(ATTACKER_HERO_POS, HeroTypeID(0), PlayerColor(0)).heroGarrison({{archer, 10}})
			.hero(DEFENDER_HERO_POS, HeroTypeID(1), PlayerColor(1)).heroGarrison({{pikeman, 10}})
			.town(INFERNO_TOWN_POS, FactionID::INFERNO, PlayerColor(1)).townGarrison({{pikeman, 10}});
		startWithMap(std::move(builder));

		attackerSideHero = findHeroAt(ATTACKER_HERO_POS);
		defenderSideHero = findHeroAt(DEFENDER_HERO_POS);
		infernoTown = expectAt<CGTownInstance>(INFERNO_TOWN_POS);
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		ASSERT_NE(infernoTown, nullptr);
		ASSERT_EQ(infernoTown->getFactionID(), FactionID::INFERNO);

		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
	}

	void buildFort()
	{
		if(!infernoTown->hasBuilt(BuildingID::FORT))
		{
			ASSERT_TRUE(gameHandler->buildStructure(infernoTown->id, BuildingID::FORT, true));
		}
		ASSERT_EQ(infernoTown->fortLevel(), CGTownInstance::FORT);
	}

	void buildBrimstone()
	{
		ASSERT_TRUE(gameHandler->buildStructure(infernoTown->id, BuildingID::SPECIAL_2, true));
		ASSERT_TRUE(infernoTown->hasBuilt(BuildingID::SPECIAL_2));
	}

	void startBattleAtTown(bool siege)
	{
		BattleSideArray<const CGHeroInstance *> heroes{attackerSideHero, defenderSideHero};
		BattleSideArray<const CArmedInstance *> armies{attackerSideHero, defenderSideHero};
		const int3 tile(4, 4, 0);
		const auto terrain = gameState()->getTile(tile)->getTerrainID();
		const std::string battlefieldName = "core:sand_shore";
		const auto battlefieldIdentifier = LIBRARY->identifiers()->getIdentifier(
			ModScope::scopeGame(), "battlefield", battlefieldName);
		ASSERT_TRUE(battlefieldIdentifier.has_value());
		const BattleField battlefield(*battlefieldIdentifier);
		const auto layout = BattleLayout::createDefaultLayout(*gameState(), armies[BattleSide::ATTACKER],
			armies[BattleSide::DEFENDER]);

		BattleStart start;
		start.battleID = gameState()->nextBattleID;
		start.info = BattleInfo::setupBattle(gameState().get(), tile, terrain, battlefield,
			armies, heroes, layout, siege ? infernoTown : nullptr);
		gameHandler->sendAndApply(start);
		ASSERT_EQ(gameState()->currentBattles.size(), 1u);
		battle()->tacticDistance = 0;
		battle()->obstacles.clear();
	}

	void acceptBattleResult()
	{
		BattleResultAccepted accepted;
		accepted.battleID = battle()->battleID;
		accepted.heroResult[BattleSide::ATTACKER].heroID = attackerSideHero->id;
		accepted.heroResult[BattleSide::DEFENDER].heroID = defenderSideHero->id;
		accepted.heroResult[BattleSide::ATTACKER].armyID = battle()->battleGetArmyObject(BattleSide::ATTACKER)->id;
		accepted.heroResult[BattleSide::DEFENDER].armyID = battle()->battleGetArmyObject(BattleSide::DEFENDER)->id;
		accepted.winnerSide = BattleSide::ATTACKER;
		gameHandler->sendAndApply(accepted);

		BattleEnded ended;
		ended.battleID = accepted.battleID;
		gameHandler->sendAndApply(ended);
	}

	CGTownInstance * infernoTown = nullptr;
};
}

TEST_F(NewHorizonsBrimstoneSiegeTest, BuiltBrimstoneRaisesOnlyDefendingSiegeHeroAndAcceptedResultEndsIt)
{
	ASSERT_NO_FATAL_FAILURE(startScenario());
	ASSERT_NO_FATAL_FAILURE(buildFort());
	ASSERT_NO_FATAL_FAILURE(buildBrimstone());
	const auto source = brimstoneSource(*infernoTown);
	const auto attackerSpellPower = attackerSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER);
	const auto defenderSpellPower = defenderSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER);

	ASSERT_NO_FATAL_FAILURE(startBattleAtTown(true));
	ASSERT_EQ(battle()->getSideHero(BattleSide::DEFENDER), defenderSideHero);
	EXPECT_EQ(defenderSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), defenderSpellPower + 20);
	EXPECT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), attackerSpellPower);
	const auto granted = brimstoneSpellPowerBonuses(*defenderSideHero, source);
	ASSERT_EQ(granted.size(), 1u);
	EXPECT_EQ(granted.front()->val, 20);
	EXPECT_TRUE(Bonus::OneBattle(granted.front()));
	EXPECT_TRUE(brimstoneSpellPowerBonuses(*attackerSideHero, source).empty());

	ASSERT_NO_FATAL_FAILURE(acceptBattleResult());
	EXPECT_EQ(defenderSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), defenderSpellPower);
	EXPECT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), attackerSpellPower);
	EXPECT_TRUE(brimstoneSpellPowerBonuses(*defenderSideHero, source).empty());
}

TEST_F(NewHorizonsBrimstoneSiegeTest, UnbuiltBrimstoneDoesNotRaiseDefendingHeroSpellPower)
{
	ASSERT_NO_FATAL_FAILURE(startScenario());
	ASSERT_NO_FATAL_FAILURE(buildFort());
	const auto source = brimstoneSource(*infernoTown);
	const auto defenderSpellPower = defenderSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER);

	ASSERT_NO_FATAL_FAILURE(startBattleAtTown(true));
	EXPECT_EQ(defenderSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), defenderSpellPower);
	EXPECT_TRUE(infernoTown->hasBuilt(BuildingID::FORT));
	EXPECT_FALSE(infernoTown->hasBuilt(BuildingID::SPECIAL_2));
	EXPECT_TRUE(brimstoneSpellPowerBonuses(*defenderSideHero, source).empty());

	BattleCancelled cancelled;
	cancelled.battleID = battle()->battleID;
	gameHandler->sendAndApply(cancelled);
	EXPECT_EQ(defenderSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), defenderSpellPower);
}

TEST_F(NewHorizonsBrimstoneSiegeTest, BuiltBrimstoneDoesNotRaiseHeroSpellPowerInFieldBattle)
{
	ASSERT_NO_FATAL_FAILURE(startScenario());
	ASSERT_NO_FATAL_FAILURE(buildFort());
	ASSERT_NO_FATAL_FAILURE(buildBrimstone());
	const auto source = brimstoneSource(*infernoTown);
	const auto attackerSpellPower = attackerSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER);
	const auto defenderSpellPower = defenderSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER);

	ASSERT_NO_FATAL_FAILURE(startBattleAtTown(false));
	EXPECT_EQ(battle()->getDefendedTown(), nullptr);
	EXPECT_EQ(defenderSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), defenderSpellPower);
	EXPECT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), attackerSpellPower);
	EXPECT_TRUE(brimstoneSpellPowerBonuses(*defenderSideHero, source).empty());
	EXPECT_TRUE(brimstoneSpellPowerBonuses(*attackerSideHero, source).empty());

	BattleCancelled cancelled;
	cancelled.battleID = battle()->battleID;
	gameHandler->sendAndApply(cancelled);
}
