/*
 * NewHorizonsUnmannedTowerTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 *
 */
#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/bonuses/BonusCustomTypes.h"
#include "../../../lib/bonuses/BonusEnum.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/battle/BattleLayout.h"
#include "../../../lib/battle/CBattleInfoCallback.h"
#include "../../../lib/constants/EntityIdentifiers.h"
#include "../../../lib/entities/hero/NewHorizonsCapabilityRules.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace
{
class NewHorizonsUnmannedTowerTest : public BattleTestFixture
{
protected:
	int towerBaseDamage = 60;
	bool capabilityRulesEnabled = true;
	CGTownInstance * fortifiedTown = nullptr;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		if(!capabilityRulesEnabled)
		{
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode());
			return;
		}

		auto rules = JsonNode(JsonPath::builtin("config/newHorizonsCapabilities"));
		rules["rulesetVersion"].Integer() = 3;
		rules["warMachineShop"] = JsonNode();
		rules["siege"]["outputs"]["defensiveTowerDamage"]["base"].Integer() = towerBaseDamage;
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, std::move(rules));
	}

	void prepareFortifiedTown()
	{
		startGame(true);
		const auto towns = gameState()->getPlayerState(PlayerColor(1))->getTowns();
		ASSERT_EQ(towns.size(), 1u);
		fortifiedTown = towns.front();
		ASSERT_EQ(fortifiedTown->fortLevel(), CGTownInstance::FORT);

		for(const auto resource : {GameResID(GameResID::WOOD), GameResID(GameResID::ORE), GameResID(GameResID::GOLD)})
			grantResources(PlayerColor(1), resource, 100000);
		ASSERT_TRUE(gameHandler->buildStructure(fortifiedTown->id, BuildingID::CITADEL));
		ASSERT_GT(fortifiedTown->fortificationsLevel().citadelHealth, 0);

		// Keep an ordinary living town-garrison stack in the real siege setup; the
		// defending side deliberately has no hero.
		fortifiedTown->addToSlot(SlotID(0), creatureByName("core:pikeman"), 20);
		attackerSideHero->clearSlots();
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), 1000));
	}

	void startUnmannedSiege()
	{
		BattleSideArray<const CGHeroInstance *> heroes{attackerSideHero, nullptr};
		BattleSideArray<const CArmedInstance *> armies{attackerSideHero, fortifiedTown};
		const auto tile = fortifiedTown->visitablePos();
		const auto layout = BattleLayout::createDefaultLayout(*gameState(), armies[BattleSide::ATTACKER],
			armies[BattleSide::DEFENDER]);
		gameHandler->battles->startBattle(armies[BattleSide::ATTACKER], armies[BattleSide::DEFENDER], tile,
			heroes[BattleSide::ATTACKER], heroes[BattleSide::DEFENDER], layout, fortifiedTown);
		ASSERT_EQ(gameState()->currentBattles.size(), 1u);
	}

	const CStack * defensiveTower() const
	{
		const auto towers = battle()->battleGetStacksIf([](const CStack * stack)
		{
			return stack->unitSide() == BattleSide::DEFENDER && stack->isTurret();
		});
		return towers.empty() ? nullptr : towers.front();
	}

	const CStack * attackerTarget() const
	{
		const auto targets = battle()->battleGetStacksIf([](const CStack * stack)
		{
			return stack->unitSide() == BattleSide::ATTACKER
				&& stack->unitType()->getId() == creatureByName("core:pikeman");
		});
		return targets.empty() ? nullptr : targets.front();
	}

	auto towerForecast(const CStack * tower, const CStack * target) const
	{
		return battle()->calculateDmgRange(BattleAttackInfo(tower, target, 0, true));
	}

	const BattleAttack * firstTowerAttack(const CStack * tower) const
	{
		const auto found = std::find_if(server.attacks.begin(), server.attacks.end(), [tower](const BattleAttack & attack)
		{
			return attack.stackAttacking == tower->unitId() && attack.shot();
		});
		return found == server.attacks.end() ? nullptr : &*found;
	}

};
}

TEST_F(NewHorizonsUnmannedTowerTest, SavedV3RulesUseCanonicalSixtyDamageWithoutADefendingHero)
{
	ASSERT_NO_FATAL_FAILURE(prepareFortifiedTown());
	ASSERT_NO_FATAL_FAILURE(startUnmannedSiege());
	EXPECT_EQ(gameState()->getHeroCapabilityRules()["rulesetVersion"].Integer(), 3);

	const auto * tower = defensiveTower();
	ASSERT_NE(tower, nullptr);
	EXPECT_EQ(battle()->getSideHero(BattleSide::DEFENDER), nullptr);
	EXPECT_EQ(battle()->battleGetOwnerHero(tower), nullptr);
	ASSERT_TRUE(fortifiedTown->getNewHorizonsDefensiveTowerBaseDamage());
	EXPECT_EQ(*fortifiedTown->getNewHorizonsDefensiveTowerBaseDamage(), 60);

	const auto * target = attackerTarget();
	ASSERT_NE(target, nullptr);
	ASSERT_TRUE(battle()->battleCanShoot(tower, target->getPosition()));
	const auto forecast = towerForecast(tower, target);
	EXPECT_EQ(forecast.damageBeforeDefense.min, 60);
	EXPECT_EQ(forecast.damageBeforeDefense.max, 60);
}

TEST_F(NewHorizonsUnmannedTowerTest, SavedV3TownDamageBaseIsUsedInsteadOfAHardcodedCanonicalValue)
{
	towerBaseDamage = 77;
	ASSERT_NO_FATAL_FAILURE(prepareFortifiedTown());
	ASSERT_NO_FATAL_FAILURE(startUnmannedSiege());
	EXPECT_EQ(gameState()->getHeroCapabilityRules()["rulesetVersion"].Integer(), 3);

	const auto * tower = defensiveTower();
	ASSERT_NE(tower, nullptr);
	EXPECT_EQ(battle()->battleGetOwnerHero(tower), nullptr);
	ASSERT_TRUE(fortifiedTown->getNewHorizonsDefensiveTowerBaseDamage());
	EXPECT_EQ(*fortifiedTown->getNewHorizonsDefensiveTowerBaseDamage(), 77);

	const auto * target = attackerTarget();
	ASSERT_NE(target, nullptr);
	const auto forecast = towerForecast(tower, target);
	EXPECT_EQ(forecast.damageBeforeDefense.min, 77);
	EXPECT_EQ(forecast.damageBeforeDefense.max, 77);
}

TEST_F(NewHorizonsUnmannedTowerTest, LegacyWorldKeepsItsExistingTownScriptDamage)
{
	capabilityRulesEnabled = false;
	ASSERT_NO_FATAL_FAILURE(prepareFortifiedTown());
	ASSERT_NO_FATAL_FAILURE(startUnmannedSiege());

	const auto * tower = defensiveTower();
	ASSERT_NE(tower, nullptr);
	EXPECT_EQ(battle()->battleGetOwnerHero(tower), nullptr);
	EXPECT_FALSE(fortifiedTown->getNewHorizonsDefensiveTowerBaseDamage());

	const auto * target = attackerTarget();
	ASSERT_NE(target, nullptr);
	const auto forecast = towerForecast(tower, target);
	const auto expectedMinimum = tower->valOfBonuses(BonusType::CREATURE_DAMAGE,
		BonusSubtypeID(BonusCustomSubtype::creatureDamageMin));
	const auto expectedMaximum = tower->valOfBonuses(BonusType::CREATURE_DAMAGE,
		BonusSubtypeID(BonusCustomSubtype::creatureDamageMax));
	EXPECT_GT(expectedMinimum, 0);
	EXPECT_GT(expectedMaximum, expectedMinimum);
	EXPECT_EQ(forecast.damageBeforeDefense.min, expectedMinimum);
	EXPECT_EQ(forecast.damageBeforeDefense.max, expectedMaximum);
	EXPECT_NE(forecast.damageBeforeDefense.min, 60);
	EXPECT_NE(forecast.damageBeforeDefense.max, 60);
}

TEST_F(NewHorizonsUnmannedTowerTest, AutomaticUnmannedTowerShotMatchesItsDamageForecast)
{
	ASSERT_NO_FATAL_FAILURE(prepareFortifiedTown());
	ASSERT_NO_FATAL_FAILURE(startUnmannedSiege());

	const auto * tower = defensiveTower();
	ASSERT_NE(tower, nullptr);
	EXPECT_EQ(battle()->battleGetOwnerHero(tower), nullptr);
	const auto * target = attackerTarget();
	ASSERT_NE(target, nullptr);
	ASSERT_TRUE(battle()->battleCanShoot(tower, target->getPosition()));
	const auto forecast = towerForecast(tower, target);
	ASSERT_EQ(forecast.damageBeforeDefense.min, 60);
	ASSERT_EQ(forecast.damageBeforeDefense.max, 60);

	const auto * attack = firstTowerAttack(tower);
	ASSERT_NE(attack, nullptr);
	ASSERT_EQ(attack->bsa.size(), 1u);
	const auto * hitTarget = battle()->getStack(attack->bsa.front().stackAttacked);
	ASSERT_NE(hitTarget, nullptr);
	const auto actualForecast = towerForecast(tower, hitTarget);
	ASSERT_EQ(actualForecast.damage.min, actualForecast.damage.max);
	EXPECT_EQ(attack->bsa.front().damageAmount, actualForecast.damage.min);
	EXPECT_EQ(actualForecast.damageBeforeDefense.min, 60);
	EXPECT_EQ(actualForecast.damageBeforeDefense.max, 60);
}
