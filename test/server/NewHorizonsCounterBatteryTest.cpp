/*
 * NewHorizonsCounterBatteryTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt file in main folder
 */
#include "../StdInc.h"
#include "battles/HeroCommandFixture.h"

#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/PossiblePlayerBattleAction.h"
#include "../../lib/entities/hero/NewHorizonsCapabilityRules.h"
#include "../../lib/mapObjects/CGTownInstance.h"

namespace
{
constexpr auto WAR_MACHINES = "new-horizons:warMachines";
constexpr auto COUNTER_BATTERY = "new-horizons:warMachines.counterBattery";
constexpr auto ENGINEER = "new-horizons:warMachines.fortificationEngineer";
}

class NewHorizonsCounterBatteryTest : public HeroCommandFixture
{
protected:
	CGTownInstance * fortifiedTown = nullptr;
	bool legacyCapabilities = false;

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		auto perks = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		auto & entries = perks["skills"][WAR_MACHINES]["perks"].Vector();
		const auto entry = std::ranges::find_if(entries, [](const auto & value)
		{
			return value["id"].String() == COUNTER_BATTERY;
		});
		if(entry == entries.end())
			throw std::runtime_error("Missing Counter-Battery perk registry entry");
		RecordProperty("counter_battery_registry_status", (*entry)["effect"]["status"].String());
		// The pre-activation gate changes only this planned row. Final reruns
		// consume an active canonical registry without replacing it.
		if((*entry)["effect"]["status"].String() == "planned")
		{
			(*entry)["effect"]["status"].String() = "active";
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
		}
		if(legacyCapabilities)
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode());
	}

	void acceptPerk(CGHeroInstance * hero, const char * perk)
	{
		const auto rank = [hero](const std::string & skill) { return hero->getPerkSkillRank(skill); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rank, seed);
			const auto selected = std::ranges::find_if(offer, [perk](const auto & value)
			{
				return value.selection.skillId == WAR_MACHINES && value.selection.perkId == perk;
			});
			if(selected == offer.end())
				continue;
			gameHandler->levelUpHero(hero, offer, std::distance(offer.begin(), selected), seed, false);
			ASSERT_TRUE(hero->hasActivePerk(WAR_MACHINES, perk));
			return;
		}
		FAIL() << "Missing legitimate War Machines offer: " << perk;
	}

	void acquire(CGHeroInstance * hero, bool selectCounterBattery = true)
	{
		const auto skill = SecondarySkill(SecondarySkill::decode(WAR_MACHINES));
		for(const auto rank : {MasteryLevel::BASIC, MasteryLevel::ADVANCED, MasteryLevel::EXPERT})
		{
			ASSERT_TRUE(hero->getPerkState().canAdvanceSkillNormally(WAR_MACHINES, hero->getSecSkillLevel(skill)));
			gameHandler->levelUpHero(hero, skill, false);
			ASSERT_EQ(hero->getPerkSkillRank(WAR_MACHINES), rank);
			if(rank == MasteryLevel::BASIC)
			{
				ASSERT_NO_FATAL_FAILURE(acceptPerk(hero, "new-horizons:warMachines.surgeon"));
			}
			else if(rank == MasteryLevel::ADVANCED)
			{
				ASSERT_NO_FATAL_FAILURE(acceptPerk(hero, "new-horizons:warMachines.piercingBolts"));
			}
			else if(selectCounterBattery)
			{
				ASSERT_NO_FATAL_FAILURE(acceptPerk(hero, COUNTER_BATTERY));
			}
		}
		ASSERT_FALSE(hero->hasActivePerk(WAR_MACHINES, ENGINEER));
	}

	void prepareTown(bool selected = true)
	{
		startGame(true);
		const auto towns = gameState()->getPlayerState(PlayerColor(1))->getTowns();
		ASSERT_EQ(towns.size(), 1u);
		fortifiedTown = towns.front();
		for(const auto resource : {GameResID(GameResID::WOOD), GameResID(GameResID::ORE), GameResID(GameResID::GOLD)})
			grantResources(PlayerColor(1), resource, 100000);
		ASSERT_TRUE(gameHandler->buildStructure(fortifiedTown->id, BuildingID::CITADEL));
		ASSERT_NO_FATAL_FAILURE(acquire(defenderSideHero, selected));
		startBattle(fortifiedTown);
	}

	const CStack * tower() const
	{
		const auto towers = battle()->battleGetStacksIf([](const CStack * unit)
		{
			return unit->unitSide() == BattleSide::DEFENDER && unit->isTurret();
		});
		return towers.empty() ? nullptr : towers.front();
	}

	void advanceUntil(const CStack * unit)
	{
		beginCombat();
		const auto round = battle()->getRound();
		for(size_t i = 0; i < battle()->stacks.size() * 4 && battle()->battleActiveUnit() != unit; ++i)
		{
			ASSERT_EQ(battle()->getRound(), round);
			const auto * active = battle()->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(active),
				BattleAction::makeDefend(active)));
		}
		ASSERT_EQ(battle()->battleActiveUnit(), unit);
	}

	DamageRange forecast(const CStack * source, const CStack * target) const
	{
		return battle()->calculateDmgRange(BattleAttackInfo(source, target, 0, true)).damage;
	}

	void expectFinalFiftyPercent(const CStack * source, const CStack * enemy, const CStack * friendly)
	{
		ASSERT_EQ(enemy->creatureId(), friendly->creatureId());
		ASSERT_EQ(enemy->getDefense(true), friendly->getDefense(true));
		ASSERT_TRUE(battle()->battleIsCounterBatteryMachineTarget(source, enemy));
		ASSERT_FALSE(battle()->battleIsCounterBatteryMachineTarget(source, friendly));
		ASSERT_FALSE(battle()->battleCanShootAction(source, friendly->getPosition()));
		// A read-only friendly twin is the bonus-off calculation control, never
		// an issued attack. One HP tolerance covers pre-final integer rounding.
		const auto baseline = forecast(source, friendly);
		const auto increased = forecast(source, enemy);
		ASSERT_GT(baseline.min, 0);
		EXPECT_GE(increased.min, baseline.min * 150 / 100);
		EXPECT_LE(increased.min, baseline.min * 150 / 100 + 1);
		EXPECT_GE(increased.max, baseline.max * 150 / 100);
		EXPECT_LE(increased.max, baseline.max * 150 / 100 + 1);
	}
};

TEST_F(NewHorizonsCounterBatteryTest, RealTowerAcceptsChosenEnemyMachineWithoutEngineerAndRejectsCreature)
{
	ASSERT_NO_FATAL_FAILURE(prepareTown());
	const auto * source = tower();
	ASSERT_NE(source, nullptr);
	ASSERT_LT(source->getPosition().toInt(), 0);
	ASSERT_EQ(battle()->getDefendedTown(), fortifiedTown);
	ASSERT_FALSE(battle()->battleCanUseFortificationEngineer(source));
	auto * chosen = addStack(BattleSide::ATTACKER, CreatureID::BALLISTA, BattleHex(92), 1);
	auto * other = addStack(BattleSide::ATTACKER, CreatureID::AMMO_CART, BattleHex(94), 1);
	auto * friendly = addStack(BattleSide::DEFENDER, CreatureID::BALLISTA, BattleHex(110), 1);
	auto * creature = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(75), 1000);
	ASSERT_TRUE(battle()->battleCanUseCounterBattery(source));
	ASSERT_TRUE(battle()->battleCounterBatteryControlsMachineTargetsOnly(source));
	ASSERT_NO_FATAL_FAILURE(expectFinalFiftyPercent(source, chosen, friendly));
	ASSERT_TRUE(defenderSideHero->getSiegeCapabilities());
	const auto base = newHorizonsHeroes::capabilitySiegeOutput(defenderSideHero->getCapabilityRules(),
		defenderSideHero->getSiegeCapabilities()->siegeRating, "defensiveTowerDamage");
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(source, chosen, 0, true)).damageBeforeDefense.min, base);
	ASSERT_NO_FATAL_FAILURE(advanceUntil(source));
	const auto actions = battle()->getClientActionsForStack(source, BattleClientInterfaceData{});
	EXPECT_TRUE(std::ranges::any_of(actions, [](const auto & action)
	{
		return action.get() == PossiblePlayerBattleAction::SHOOT;
	}));
	ASSERT_FALSE(battle()->battleCanShootAction(source, creature->getPosition()));
	const auto attacksBefore = server.attacks.size();
	const auto * activeBefore = battle()->battleActiveUnit();
	const auto creatureHealth = creature->getAvailableHealth();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeShotAttack(source, creature)));
	EXPECT_EQ(creature->getAvailableHealth(), creatureHealth);
	// The handler announces an attempted action before validation. Rejection
	// must instead leave the active unit and authoritative attack output intact.
	EXPECT_EQ(battle()->battleActiveUnit(), activeBefore);
	EXPECT_EQ(server.attacks.size(), attacksBefore);
	const auto expected = forecast(source, chosen);
	const auto chosenHealth = chosen->getAvailableHealth();
	const auto otherHealth = other->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeShotAttack(source, chosen)));
	EXPECT_GE(chosenHealth - chosen->getAvailableHealth(), expected.min);
	EXPECT_LE(chosenHealth - chosen->getAvailableHealth(), expected.max);
	EXPECT_EQ(other->getAvailableHealth(), otherHealth);
}

TEST_F(NewHorizonsCounterBatteryTest, BallistaMachineShotAddsFinalDamageButCreatureShotDoesNot)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(acquire(attackerSideHero));
	giveArtifact(attackerSideHero, ArtifactID::BALLISTA, ArtifactPosition::MACH1);
	startBattle();
	const auto machines = battle()->battleGetStacksIf([](const CStack * unit)
	{
		return unit->unitSide() == BattleSide::ATTACKER && unit->isBallista();
	});
	ASSERT_EQ(machines.size(), 1u);
	const auto * source = machines.front();
	auto * enemyMachine = addStack(BattleSide::DEFENDER, CreatureID::BALLISTA, BattleHex(92), 1);
	auto * friendlyMachine = addStack(BattleSide::ATTACKER, CreatureID::BALLISTA, BattleHex(110), 1);
	auto * enemyCreature = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(94), 1000);
	auto * friendlyCreature = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(77), 1000);
	ASSERT_NO_FATAL_FAILURE(expectFinalFiftyPercent(source, enemyMachine, friendlyMachine));
	EXPECT_FALSE(battle()->battleIsCounterBatteryMachineTarget(source, enemyCreature));
	ASSERT_EQ(battle()->battleHasDistancePenalty(source, source->getPosition(), enemyCreature->getPosition()),
		battle()->battleHasDistancePenalty(source, source->getPosition(), friendlyCreature->getPosition()));
	EXPECT_EQ(forecast(source, enemyCreature).min, forecast(source, friendlyCreature).min);
	EXPECT_EQ(forecast(source, enemyCreature).max, forecast(source, friendlyCreature).max);
	ASSERT_NO_FATAL_FAILURE(advanceUntil(source));
	const auto expected = forecast(source, enemyMachine);
	const auto before = enemyMachine->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(source, enemyMachine)));
	// Injury cannot exceed the target's remaining health even when the final
	// boosted shot's damage range is larger than a one-machine stack's HP.
	EXPECT_GE(before - enemyMachine->getAvailableHealth(), std::min(expected.min, before));
	EXPECT_LE(before - enemyMachine->getAvailableHealth(), std::min(expected.max, before));
}

TEST_F(NewHorizonsCounterBatteryTest, NoLegalEnemyMachineRetainsOrdinaryAutomaticTowerActivation)
{
	ASSERT_NO_FATAL_FAILURE(prepareTown());
	const auto * source = tower();
	ASSERT_NE(source, nullptr);
	BattleUnitsChanged removed;
	removed.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
	{
		if(unit->unitSide() == BattleSide::ATTACKER
			&& (unit->isBallista() || unit->isAmmoCart() || unit->isFirstAidTent() || unit->isCatapult()))
		{
			removed.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		}
	}
	gameHandler->sendAndApply(removed);
	addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(92), 1000);
	ASSERT_TRUE(battle()->battleCanUseCounterBattery(source));
	EXPECT_FALSE(battle()->battleHasCounterBatteryMachineTarget(source));
	EXPECT_FALSE(battle()->battleCounterBatteryControlsMachineTargetsOnly(source));
	beginCombat();
	endRound();
	EXPECT_TRUE(std::ranges::any_of(server.attacks, [source](const auto & attack)
	{
		return attack.stackAttacking == source->unitId() && attack.shot();
	})) << "No legal machine pool must leave the real tower's ordinary CPU shot intact";
}

TEST_F(NewHorizonsCounterBatteryTest, UnselectedExpertDoesNotGainTowerControlOrMachineMultiplier)
{
	ASSERT_NO_FATAL_FAILURE(prepareTown(false));
	const auto * source = tower();
	ASSERT_NE(source, nullptr);
	auto * enemy = addStack(BattleSide::ATTACKER, CreatureID::BALLISTA, BattleHex(92), 1);
	auto * friendly = addStack(BattleSide::DEFENDER, CreatureID::BALLISTA, BattleHex(110), 1);
	EXPECT_FALSE(battle()->battleCanUseCounterBattery(source));
	EXPECT_FALSE(battle()->battleHasCounterBatteryMachineTarget(source));
	EXPECT_FALSE(battle()->battleCounterBatteryControlsMachineTargetsOnly(source));
	EXPECT_EQ(forecast(source, enemy).min, forecast(source, friendly).min);
	EXPECT_EQ(forecast(source, enemy).max, forecast(source, friendly).max);
}

TEST_F(NewHorizonsCounterBatteryTest, LegacyCapabilitySnapshotDoesNotEnableSelectedCounterBattery)
{
	legacyCapabilities = true;
	ASSERT_NO_FATAL_FAILURE(prepareTown());
	ASSERT_TRUE(defenderSideHero->hasActivePerk(WAR_MACHINES, COUNTER_BATTERY));
	const auto * source = tower();
	ASSERT_NE(source, nullptr);
	auto * enemy = addStack(BattleSide::ATTACKER, CreatureID::BALLISTA, BattleHex(92), 1);
	EXPECT_FALSE(battle()->battleCanUseCounterBattery(source));
	EXPECT_FALSE(battle()->battleIsCounterBatteryMachineTarget(source, enemy));
	EXPECT_FALSE(battle()->battleCounterBatteryControlsMachineTargetsOnly(source));
}
