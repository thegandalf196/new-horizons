/*
 * NewHorizonsMachineSpecialtyAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../server/battles/NewHorizonsMachineSpecialtyFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleAI.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/SelectedAttackReceipt.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/entities/hero/NewHorizonsCapabilityRules.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <algorithm>
#include <memory>
#include <vector>

namespace
{
class MachineAIEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit MachineAIEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class MachineAIRecordingCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> unitActions;
	std::vector<BattleAction> heroActions;
	MachineAIRecordingCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override { heroActions.push_back(action); }
	void battleMakeUnitAction(const BattleID &, const BattleAction & action) override { unitActions.push_back(action); }
};

template<typename Random>
auto rngBytes(Random & random)
{
	CMemorySerializer wire;
	wire.oser & random;
	return wire.extractBuffer();
}

auto queueIds(const CBattleInfoCallback & callback, int turn = 0)
{
	std::vector<battle::Units> rounds;
	callback.battleGetTurnOrder(rounds, 0, 2, turn);
	std::vector<std::vector<uint32_t>> ids;
	for(const auto & round : rounds)
	{
		auto & row = ids.emplace_back();
		for(const auto * unit : round)
			row.push_back(unit->unitId());
	}
	return ids;
}
}

class NewHorizonsMachineSpecialtyAITest : public NewHorizonsMachineSpecialtyFixture
{
protected:
	std::shared_ptr<MachineAIEnvironment> environment;
	std::shared_ptr<CPlayerBattleCallback> callback;

	std::shared_ptr<HypotheticBattle> simulation()
	{
		if(!environment)
			environment = std::make_shared<MachineAIEnvironment>(gameState());
		callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		return std::make_shared<HypotheticBattle>(environment.get(), callback);
	}

	void TearDown() override
	{
		callback.reset();
		environment.reset();
		NewHorizonsMachineSpecialtyFixture::TearDown();
	}

	std::shared_ptr<MachineAIRecordingCallback> choosePendingShot()
	{
		auto recorded = std::make_shared<MachineAIRecordingCallback>();
		recorded->onBattleStarted(battle());
		CBattleAI ai;
		ai.initBattleInterface(environment, recorded);
		ai.battleStart(BattleID(0), attackerSideHero, defenderSideHero, int3(4, 4, 0),
			attackerSideHero, defenderSideHero, BattleSide::ATTACKER, false);
		ai.activeStack(BattleID(0), actor);
		return recorded;
	}

	void assertForecastReadOnly(const std::shared_ptr<HypotheticBattle> & pending,
		DamageCache & cache, int expectedPercent, int64_t & projectedDamage)
	{
		auto sibling = std::make_shared<HypotheticBattle>(environment.get(), pending);
		auto copiedActor = pending->getForUpdate(actor->unitId());
		auto copiedTarget = pending->getForUpdate(victim->unitId());
		const auto actorBefore = copiedActor->save();
		const auto targetBefore = copiedTarget->save();
		const auto siblingBefore = sibling->getForUpdate(actor->unitId())->save();
		const auto liveBefore = gameState()->saveToMemory();
		const auto serverRng = rngBytes(*gameHandler->randomizer);
		const auto aiRng = rngBytes(CRandomGenerator::getDefault());
		const BattleAttackInfo input(copiedActor.get(), copiedTarget.get(), 0, true);
		const auto forecast = battle()->calculateDmgRange(BattleAttackInfo(actor, victim, 0, true)).damage;
		ASSERT_EQ(forecast.min, forecast.max); // Fixed machine output; fixture disables Luck.
		EXPECT_EQ(pending->battleGetRangedFollowUpDamagePercent(copiedActor.get()), expectedPercent);
		EXPECT_EQ(cache.getDamage(copiedActor.get(), copiedTarget.get(), pending),
			pending->battleExpectedLuckDamage(input));
		const auto preview = AttackPossibility::evaluate(input, BattleHex::INVALID, cache, pending);
		EXPECT_EQ(AttackPossibility::getAttackCount(*copiedActor, true, *pending), 1);
		ASSERT_NE(preview.attackerState, nullptr);
		ASSERT_EQ(preview.fortuneStrikes.size(), 1u);
		for(const auto & [id, amount] : preview.fortuneStrikes.front().resolvedHits)
			if(id == victim->unitId())
				projectedDamage += amount;
		EXPECT_EQ(projectedDamage, forecast.min);
		EXPECT_EQ(copiedActor->save(), actorBefore);
		EXPECT_EQ(copiedTarget->save(), targetBefore);
		EXPECT_EQ(sibling->getForUpdate(actor->unitId())->save(), siblingBefore);
		EXPECT_EQ(gameState()->saveToMemory(), liveBefore);
		EXPECT_EQ(rngBytes(*gameHandler->randomizer), serverRng);
		EXPECT_EQ(rngBytes(CRandomGenerator::getDefault()), aiRng);
		auto selected = std::make_shared<HypotheticBattle>(environment.get(), pending);
		auto selectedActor = selected->getForUpdate(actor->unitId());
		selectedAttackReceipt::restoreResources(preview, *preview.attackerState,
			selectedActor, selectedActor, *selected);
		EXPECT_EQ(selectedActor->rangedFollowUpDamagePercent, 0);
		EXPECT_EQ(copiedActor->rangedFollowUpDamagePercent, expectedPercent);
	}

	void assertPendingParity(int mode, int expectedPercent)
	{
		ASSERT_NO_FATAL_FAILURE(prepare("core:torosar", mode));
		ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:warMachines",
			"new-horizons:warMachines.masterGunner"));
		forceActorFirst();
		beginCombat();
		ASSERT_EQ(battle()->battleActiveUnit(), actor);
		// Spend the real Hero Action before invoking the creature AI.
		ASSERT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE));
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE)));
		ASSERT_EQ(battle()->battleActiveUnit(), actor);
		auto source = simulation();
		DamageCache cache;
		cache.cacheDamage(source->battleGetUnitByID(actor->unitId()),
			source->battleGetUnitByID(victim->unitId()), source);
		const auto serial = battle()->getActivationSerial();
		const auto firstAmmo = actor->acquireState()->shots.available();
		ASSERT_TRUE(actor->acquireState()->shots.isLimited());
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeShotAttack(actor, victim)));
		ASSERT_TRUE(battle()->battleHasPendingRangedFollowUp(actor));
		ASSERT_EQ(battle()->battleGetRangedFollowUpDamagePercent(actor), expectedPercent);
		EXPECT_EQ(battle()->getActivationSerial(), serial);
		EXPECT_EQ(actor->acquireState()->shots.available(), firstAmmo - 1);
		auto pending = std::make_shared<HypotheticBattle>(environment.get(), callback);
		int64_t projectedDamage = 0;
		ASSERT_NO_FATAL_FAILURE(assertForecastReadOnly(pending, cache, expectedPercent, projectedDamage));
		const auto liveBefore = gameState()->saveToMemory();
		const auto serverRng = rngBytes(*gameHandler->randomizer);
		const auto recorded = choosePendingShot();
		ASSERT_TRUE(recorded->heroActions.empty());
		ASSERT_EQ(recorded->unitActions.size(), 1u);
		const auto & action = recorded->unitActions.front();
		ASSERT_EQ(action.actionType, EActionType::SHOOT);
		ASSERT_EQ(action.stackNumber, actor->unitId());
		const auto target = action.getTarget(battle());
		ASSERT_FALSE(target.empty());
		const auto * selected = target.front().unitValue ? target.front().unitValue
			: battle()->battleGetUnitByPos(target.front().hexValue);
		ASSERT_NE(selected, nullptr);
		ASSERT_EQ(selected->unitId(), victim->unitId());
		EXPECT_EQ(gameState()->saveToMemory(), liveBefore);
		EXPECT_EQ(rngBytes(*gameHandler->randomizer), serverRng);
		const auto health = victim->getAvailableHealth();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		EXPECT_EQ(health - victim->getAvailableHealth(), projectedDamage);
		EXPECT_EQ(actor->acquireState()->shots.available(), firstAmmo - 2);
		EXPECT_EQ(actor->acquireState()->rangedFollowUpDamagePercent, 0);
		EXPECT_FALSE(battle()->battleHasPendingRangedFollowUp(actor));
		EXPECT_EQ(std::ranges::count_if(server.attacks, [&](const auto & shot)
			{ return shot.stackAttacking == actor->unitId() && shot.shot(); }), 2);
	}

};

TEST_F(NewHorizonsMachineSpecialtyAITest, CapturedTorosarPendingEightyUsesDynamicCacheAndAuthority)
{
	ASSERT_NO_FATAL_FAILURE(assertPendingParity(0, 80));
}

TEST_F(NewHorizonsMachineSpecialtyAITest, MissingTorosarPolicyRetainsSixtyPercentAIAndAuthority)
{
	ASSERT_NO_FATAL_FAILURE(assertPendingParity(1, 60));
}

TEST_F(NewHorizonsMachineSpecialtyAITest, ZeroTorosarPolicyRetainsSixtyPercentAIAndAuthority)
{
	ASSERT_NO_FATAL_FAILURE(assertPendingParity(2, 60));
}

TEST_F(NewHorizonsMachineSpecialtyAITest, CapturedInitiativeUsesSameNativeAndHypotheticalQueue)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:torosar"));
	ASSERT_NE(upgraded, nullptr);
	ASSERT_EQ(actor->valOfBonuses(BonusType::STACKS_INITIATIVE_FLAT), 2);
	ASSERT_EQ(upgraded->valOfBonuses(BonusType::STACKS_INITIATIVE_FLAT), 0);
	// Put an ordinary same-phase unit between the two Ballista initiatives.
	for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
		upgraded->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::STACKS_INITIATIVE_BASE, BonusSource::OTHER, actor->getInitiative() - 1,
			BonusSourceID(), BonusSubtypeID(), bound));
	ASSERT_EQ(upgraded->getInitiative(), actor->getInitiative() - 1);
	beginCombat();
	auto branch = simulation();
	EXPECT_EQ(branch->battleGetUnitByID(actor->unitId())->getInitiative(), actor->getInitiative());
	EXPECT_EQ(queueIds(*branch, 1), queueIds(*battle(), 1));
	auto without = std::make_shared<HypotheticBattle>(environment.get(), branch);
	auto unboosted = without->getForUpdate(actor->unitId());
	unboosted->removeUnitBonus(CSelector([](const Bonus * bonus)
		{ return bonus->stacking == "new-horizons:ordinary-creature-specialty:core:torosar"; }));
	EXPECT_EQ(actor->getInitiative() - unboosted->getInitiative(), 2);
	EXPECT_EQ(unboosted->getMovementRange(), actor->getMovementRange());
	const auto boostedQueue = queueIds(*branch, 1);
	const auto ordinaryQueue = queueIds(*without, 1);
	ASSERT_FALSE(boostedQueue.empty());
	ASSERT_FALSE(ordinaryQueue.empty());
	const auto & boosted = boostedQueue.front();
	const auto & ordinary = ordinaryQueue.front();
	const auto boostedActor = std::ranges::find(boosted, actor->unitId());
	const auto boostedOther = std::ranges::find(boosted, upgraded->unitId());
	const auto ordinaryActor = std::ranges::find(ordinary, actor->unitId());
	const auto ordinaryOther = std::ranges::find(ordinary, upgraded->unitId());
	ASSERT_NE(boostedActor, boosted.end());
	ASSERT_NE(boostedOther, boosted.end());
	ASSERT_NE(ordinaryActor, ordinary.end());
	ASSERT_NE(ordinaryOther, ordinary.end());
	EXPECT_LT(boostedActor, boostedOther);
	EXPECT_GT(ordinaryActor, ordinaryOther);
	EXPECT_EQ(actor->valOfBonuses(BonusType::STACKS_INITIATIVE_FLAT), 2);
}

TEST_F(NewHorizonsMachineSpecialtyAITest, HeavyBatteryFlowsThroughExistingDamageCacheAndAuthority)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:gurnisson"));
	forceActorFirst();
	beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	auto branch = simulation();
	const auto * projectedActor = branch->battleGetUnitByID(actor->unitId());
	const auto * projectedTarget = branch->battleGetUnitByID(victim->unitId());
	const BattleAttackInfo input(projectedActor, projectedTarget, 0, true);
	const auto live = battle()->calculateDmgRange(BattleAttackInfo(actor, victim, 0, true)).damage;
	const auto projected = branch->calculateDmgRange(input).damage;
	EXPECT_EQ(projected.min, live.min);
	EXPECT_EQ(projected.max, live.max);
	DamageCache cache;
	cache.cacheDamage(projectedActor, projectedTarget, branch);
	EXPECT_EQ(cache.getDamage(projectedActor, projectedTarget, branch), branch->battleExpectedLuckDamage(input));
	const auto health = victim->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(actor, victim)));
	EXPECT_GE(health - victim->getAvailableHealth(), live.min);
	EXPECT_LE(health - victim->getAvailableHealth(), live.max);
}

TEST_F(NewHorizonsMachineSpecialtyAITest, TorosarWithoutLearnedMasterGunnerCreatesNoProjectedOrRealAllowance)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:torosar", 0, false));
	ASSERT_FALSE(attackerSideHero->hasActivePerk("new-horizons:warMachines", "new-horizons:warMachines.masterGunner"));
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:warMachines", "new-horizons:warMachines.precisionBombardment"));
	forceActorFirst();
	beginCombat();
	auto branch = simulation();
	const auto * projectedActor = branch->battleGetUnitByID(actor->unitId());
	DamageCache cache;
	const auto preview = AttackPossibility::evaluate(BattleAttackInfo(projectedActor,
		branch->battleGetUnitByID(victim->unitId()), 0, true), BattleHex::INVALID, cache, branch);
	ASSERT_NE(preview.attackerState, nullptr);
	EXPECT_EQ(AttackPossibility::getAttackCount(*projectedActor, true, *branch), 1);
	EXPECT_EQ(preview.attackerState->rangedFollowUpDamagePercent, 0);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(actor, victim)));
	EXPECT_FALSE(battle()->battleHasPendingRangedFollowUp(actor));
	EXPECT_EQ(actor->acquireState()->rangedFollowUpDamagePercent, 0);
}

TEST_F(NewHorizonsMachineSpecialtyAITest, HeavyBatteryControlledOwnerRequeriesExistingDamageCache)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:gurnisson"));
	forceActorFirst();
	beginCombat();
	simulation(); // Retain the environment; this test explicitly authorizes both heroes.
	const auto allKnowing = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), allKnowing);
	auto controlled = branch->getForUpdate(actor->unitId());
	auto target = branch->getForUpdate(victim->unitId());
	const auto liveBefore = gameState()->saveToMemory();
	const auto serverRng = rngBytes(*gameHandler->randomizer);
	DamageCache cache;
	cache.cacheDamage(controlled.get(), target.get(), branch);
	const auto before = cache.getDamage(controlled.get(), target.get(), branch);
	const Bonus hypnotized(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	controlled->addUnitBonus({hypnotized});
	target->addUnitBonus({hypnotized});
	ASSERT_EQ(branch->battleGetOwnerHero(controlled.get()), defenderSideHero);
	// Native targeting compares the attacker's effective owner against the
	// target's original side. Hypnotizing both does not make this a legal shot.
	ASSERT_TRUE(branch->battleMatchOwner(controlled.get(), target.get(), true));
	const bool shooting = branch->battleCanShoot(controlled.get(), target->getPosition());
	ASSERT_FALSE(shooting);
	const BattleAttackInfo input(controlled.get(), target.get(), 0, shooting);
	const auto current = branch->battleExpectedLuckDamage(input);
	const auto forcedShot = branch->battleExpectedLuckDamage(BattleAttackInfo(controlled.get(), target.get(), 0, true));
	ASSERT_GT(forcedShot, current) << "A forced shot is not the cache's inferred attack mode";
	ASSERT_NE(current, before) << "This fixture must expose stale cached owner output";
	DamageCacheProbeTiming timing;
	EXPECT_EQ(cache.getDamage(controlled.get(), target.get(), branch, &timing), current);
	EXPECT_GT(timing.dynamicBypasses, 0u);
	EXPECT_EQ(gameState()->saveToMemory(), liveBefore);
	EXPECT_EQ(rngBytes(*gameHandler->randomizer), serverRng);
}

TEST_F(NewHorizonsMachineSpecialtyAITest, ActualCatapultCallbackUsesEnhancedTermButCyclopsDoesNot)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:gurnisson"));
	const auto siege = attackerSideHero->getSiegeCapabilities();
	ASSERT_TRUE(siege);
	const int ordinary = newHorizonsHeroes::capabilitySiegeOutput(attackerSideHero->getCapabilityRules(),
		siege->siegeRating, "catapultStructuralDamage");
	ASSERT_GT(siege->catapultStructuralDamage, ordinary);
	auto * catapult = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::CATAPULT), BattleHex(12, 5), 1);
	auto * cyclops = addStack(BattleSide::ATTACKER, creatureByName("core:cyclop"), BattleHex(12, 7), 10);
	ASSERT_NE(catapult, nullptr);
	ASSERT_NE(cyclops, nullptr);
	ASSERT_TRUE(catapult->isCatapult());
	ASSERT_FALSE(cyclops->isCatapult());
	auto branch = simulation();
	for(const auto * unit : {catapult, cyclops})
	{
		const auto * copy = branch->battleGetUnitByID(unit->unitId());
		const int expected = unit == catapult ? siege->catapultStructuralDamage : ordinary;
		EXPECT_EQ(battle()->battleGetCatapultStructuralDamage(unit, 1), expected);
		EXPECT_EQ(branch->battleGetCatapultStructuralDamage(copy, 1), expected);
		EXPECT_EQ(branch->battleGetCatapultStructuralDamage(copy, 2), 2 * expected);
		EXPECT_EQ(branch->battleGetCatapultStructuralDamage(copy, 0), 0);
	}
	// Actual battle-bound machine identity and callback routing; no wall attack is claimed.
}
