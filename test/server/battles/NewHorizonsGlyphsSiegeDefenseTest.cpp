/*
 * NewHorizonsGlyphsSiegeDefenseTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
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
constexpr int3 FORTRESS_TOWN_POS{18, 27, 0};

BonusSourceID glyphsSource(const CGTownInstance & town)
{
	const auto & building = town.getTown()->buildings.at(BuildingID::SPECIAL_3);
	return BonusSourceID(building->getUniqueTypeID());
}

std::vector<const Bonus *> defendingHeroDefenseBonuses(const CGHeroInstance & hero, const BonusSourceID & source)
{
	std::vector<const Bonus *> result;
	const auto bonuses = hero.getBonuses(Selector::source(BonusSource::TOWN_STRUCTURE, source)
		.And(Selector::type()(BonusType::PRIMARY_SKILL)));
	for(const auto & bonus : *bonuses)
	{
		if(bonus && bonus->subtype == BonusSubtypeID(PrimarySkill::DEFENSE))
			result.push_back(bonus.get());
	}
	return result;
}

class NewHorizonsGlyphsSiegeDefenseTest : public BattleTestFixture
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
		const CreatureID pikeman(CreatureID::decode("core:pikeman"));
		const auto fortress = FactionID(FactionID::decode("core:fortress"));
		ASSERT_GE(pikeman.getNum(), 0);
		ASSERT_TRUE(fortress.isValid());

		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NewHorizonsGlyphsSiegeDefense")
			.playerActive(PlayerColor(0))
			.playerActive(PlayerColor(1))
			.hero(ATTACKER_HERO_POS, HeroTypeID(0), PlayerColor(0)).heroGarrison({{pikeman, 10}})
			.hero(DEFENDER_HERO_POS, HeroTypeID(1), PlayerColor(1)).heroGarrison({{pikeman, 10}})
			.town(FORTRESS_TOWN_POS, fortress, PlayerColor(1)).townGarrison({{pikeman, 10}});
		startWithMap(std::move(builder));

		attackerSideHero = findHeroAt(ATTACKER_HERO_POS);
		defenderSideHero = findHeroAt(DEFENDER_HERO_POS);
		fortressTown = expectAt<CGTownInstance>(FORTRESS_TOWN_POS);
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		ASSERT_NE(fortressTown, nullptr);
		ASSERT_EQ(fortressTown->getFactionID(), FactionID::FORTRESS);

		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
	}

	void buildFortressPrerequisites()
	{
		if(!fortressTown->hasBuilt(BuildingID::FORT))
		{
			ASSERT_TRUE(gameHandler->buildStructure(fortressTown->id, BuildingID::FORT, true));
		}
		ASSERT_TRUE(fortressTown->hasBuilt(BuildingID::FORT));
		if(!fortressTown->hasBuilt(BuildingID::SPECIAL_2))
		{
			ASSERT_TRUE(gameHandler->buildStructure(fortressTown->id, BuildingID::SPECIAL_2, true));
		}
		ASSERT_TRUE(fortressTown->hasBuilt(BuildingID::SPECIAL_2));
	}

	void buildGlyphs()
	{
		ASSERT_NO_FATAL_FAILURE(buildFortressPrerequisites());
		ASSERT_TRUE(gameHandler->buildStructure(fortressTown->id, BuildingID::SPECIAL_3, true));
		ASSERT_TRUE(fortressTown->hasBuilt(BuildingID::SPECIAL_3));
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
			armies, heroes, layout, siege ? fortressTown : nullptr);
		gameHandler->sendAndApply(start);
		ASSERT_EQ(gameState()->currentBattles.size(), 1u);
		battle()->tacticDistance = 0;
		battle()->obstacles.clear();
	}

	void cancelBattle()
	{
		BattleCancelled cancelled;
		cancelled.battleID = battle()->battleID;
		gameHandler->sendAndApply(cancelled);
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

	CGTownInstance * fortressTown = nullptr;
};
}

TEST_F(NewHorizonsGlyphsSiegeDefenseTest, BuiltGlyphsGrantTwentyDefenseOnlyToDefendingSiegeHeroAndResultCleanupRemovesIt)
{
	ASSERT_NO_FATAL_FAILURE(startScenario());
	ASSERT_NO_FATAL_FAILURE(buildGlyphs());
	const auto source = glyphsSource(*fortressTown);
	const auto attackerDefense = attackerSideHero->getPrimSkillLevel(PrimarySkill::DEFENSE);
	const auto defenderDefense = defenderSideHero->getPrimSkillLevel(PrimarySkill::DEFENSE);
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));

	ASSERT_NO_FATAL_FAILURE(startBattleAtTown(true));
	ASSERT_EQ(battle()->getDefendedTown(), fortressTown);
	ASSERT_EQ(battle()->getSideHero(BattleSide::DEFENDER), defenderSideHero);
	EXPECT_EQ(defenderSideHero->getPrimSkillLevel(PrimarySkill::DEFENSE), defenderDefense + 20);
	EXPECT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::DEFENSE), attackerDefense);
	const auto granted = defendingHeroDefenseBonuses(*defenderSideHero, source);
	ASSERT_EQ(granted.size(), 1u);
	EXPECT_EQ(granted.front()->val, 20);
	EXPECT_TRUE(Bonus::OneBattle(granted.front()));
	EXPECT_TRUE(defendingHeroDefenseBonuses(*attackerSideHero, source).empty());

	const auto defenderPikemen = battle()->getStacksIf([&](const CStack * stack)
	{
		return stack->unitSide() == BattleSide::DEFENDER && stack->unitType()->getId() == pikeman;
	});
	const auto attackerPikemen = battle()->getStacksIf([&](const CStack * stack)
	{
		return stack->unitSide() == BattleSide::ATTACKER && stack->unitType()->getId() == pikeman;
	});
	ASSERT_FALSE(defenderPikemen.empty());
	ASSERT_FALSE(attackerPikemen.empty());
	EXPECT_EQ(defenderPikemen.front()->getDefense(false), attackerPikemen.front()->getDefense(false))
		<< "Glyphs modifies the defending hero, not the ordinary creature Defense stat";
	EXPECT_TRUE(defenderPikemen.front()->getBonuses(Selector::source(BonusSource::TOWN_STRUCTURE, source))->empty());

	ASSERT_NO_FATAL_FAILURE(acceptBattleResult());
	EXPECT_EQ(defenderSideHero->getPrimSkillLevel(PrimarySkill::DEFENSE), defenderDefense);
	EXPECT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::DEFENSE), attackerDefense);
	EXPECT_TRUE(defendingHeroDefenseBonuses(*defenderSideHero, source).empty());
}

TEST_F(NewHorizonsGlyphsSiegeDefenseTest, UnbuiltGlyphsDoNotRaiseTheDefendingSiegeHeroDefense)
{
	ASSERT_NO_FATAL_FAILURE(startScenario());
	ASSERT_NO_FATAL_FAILURE(buildFortressPrerequisites());
	const auto source = glyphsSource(*fortressTown);
	const auto defenderDefense = defenderSideHero->getPrimSkillLevel(PrimarySkill::DEFENSE);

	ASSERT_NO_FATAL_FAILURE(startBattleAtTown(true));
	EXPECT_EQ(battle()->getDefendedTown(), fortressTown);
	EXPECT_EQ(defenderSideHero->getPrimSkillLevel(PrimarySkill::DEFENSE), defenderDefense);
	EXPECT_FALSE(fortressTown->hasBuilt(BuildingID::SPECIAL_3));
	EXPECT_TRUE(defendingHeroDefenseBonuses(*defenderSideHero, source).empty());

	ASSERT_NO_FATAL_FAILURE(cancelBattle());
	EXPECT_EQ(defenderSideHero->getPrimSkillLevel(PrimarySkill::DEFENSE), defenderDefense);
}

TEST_F(NewHorizonsGlyphsSiegeDefenseTest, BuiltGlyphsDoNotRaiseHeroDefenseInAFieldBattle)
{
	ASSERT_NO_FATAL_FAILURE(startScenario());
	ASSERT_NO_FATAL_FAILURE(buildGlyphs());
	const auto source = glyphsSource(*fortressTown);
	const auto attackerDefense = attackerSideHero->getPrimSkillLevel(PrimarySkill::DEFENSE);
	const auto defenderDefense = defenderSideHero->getPrimSkillLevel(PrimarySkill::DEFENSE);

	ASSERT_NO_FATAL_FAILURE(startBattleAtTown(false));
	EXPECT_EQ(battle()->getDefendedTown(), nullptr);
	EXPECT_EQ(defenderSideHero->getPrimSkillLevel(PrimarySkill::DEFENSE), defenderDefense);
	EXPECT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::DEFENSE), attackerDefense);
	EXPECT_TRUE(defendingHeroDefenseBonuses(*defenderSideHero, source).empty());
	EXPECT_TRUE(defendingHeroDefenseBonuses(*attackerSideHero, source).empty());

	ASSERT_NO_FATAL_FAILURE(cancelBattle());
}
