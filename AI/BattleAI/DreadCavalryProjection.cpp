/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "DreadCavalryProjection.h"
#include "AttackPossibility.h"
#include "PotentialTargets.h"
#include "BattleExchangeVariant.h"
#include "../../lib/battle/ReachabilityInfo.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/NewHorizonsDreadCavalry.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsOpportunist.h"
#include "../../lib/battle/NewHorizonsShroud.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
namespace dreadCavalryAI
{
bool hasActivationState(const battle::Unit * unit)
{
	const auto * state = dynamic_cast<const battle::CUnitState *>(unit);
	return state && (state->dreadCavalryState.pending || state->dreadCavalryState.usedThisActivation);
}
void retireForActorHandoff(HypotheticBattle & world, uint32_t nextActor, BattleUnitTurnReason reason)
{
	const int previousId = world.getActiveStackID();
	if(previousId < 0 || static_cast<uint32_t>(previousId) == nextActor || reason == BattleUnitTurnReason::CRISIS_ORDER)
		return;
	const auto * previous = dynamic_cast<const battle::CUnitState *>(world.battleGetUnitByID(previousId));
	if(previous && previous->dreadCavalryState.pending
		&& newHorizonsDreadCavalry::abandonsPendingOnActorHandoff(*world.getBattle(), nextActor, reason))
		world.getForUpdate(previousId)->dreadCavalryState.retirePending();
}
bool acceptsBlow(const std::shared_ptr<HypotheticBattle> & model, const Action & action,
	const battle::Unit * source, const battle::Unit * target)
{
	return !action.before.pending || (model && newHorizonsDreadCavalry::acceptedBlowStillLegal(
		*model, source, target, action.actor, action.before));
}
void captureOutput(const CBattleInfoCallback & battle, BattleAttackInfo & attack)
{
	if(!attack.shooting && !attack.retaliation && !attack.bracePreemptive
		&& attack.physicalDamage && attack.dreadCavalryDamagePercent == 100)
		attack.dreadCavalryDamagePercent = battle.battleGetDreadCavalryDamagePercent(attack.attacker);
}
Action begin(const CBattleInfoCallback & battle, const BattleAttackInfo & attack, BattleHex from)
{
	Action result;
	const auto * state = dynamic_cast<const battle::CUnitState *>(attack.attacker);
	if(!state || attack.shooting || !attack.physicalDamage || attack.retaliation
		|| attack.bracePreemptive || attack.preemptiveDamagePercent > 0 || attack.cleaveDamagePercent > 0)
		return result;
	result.actor = state->unitId();
	result.before = state->dreadCavalryState;
	result.controller = battle.playerToSide(battle.battleGetActionController(state));
	result.luckyBefore = state->luckyOwnAttackSequence;
	result.enabled = result.before.pending ? battle.battleHasPendingDreadCavalry(state)
		: !result.before.usedThisActivation && newHorizonsDreadCavalry::entitlement(battle, state).has_value();
	result.movementBudget = state->getMovementRange(0);
	if(result.before.pending)
		result.movementBudget = std::min(result.movementBudget, result.before.remainingMovement);
	if(from.isAvailable() && from != state->getPosition())
	{
		const auto reach = battle.getReachability(state);
		const auto cost = reach.distances[from.toInt()];
		result.movementCost = cost < ReachabilityInfo::INFINITE_DIST ? static_cast<int>(cost) : result.movementBudget;
	}
	return result;
}
void observeDeath(const CBattleInfoCallback & battle, Action & action, const battle::Unit & source,
	const battle::Unit & target, bool wasAlive, bool mayRebirth)
{
	if(!action.enabled || !wasAlive || target.alive() || mayRebirth
		|| !battle.battleMatchOwner(&source, &target) || !battle.battleMatchActionController(&source, &target))
		return;
	action.anyKilled = true;
	if(!action.before.pending && action.firstVictim == battle::DreadCavalryState::NO_VICTIM)
		action.firstVictim = target.unitId();
}
void finish(AttackPossibility & preview, const Action & action, bool vanishKill)
{
	if(!action.enabled || !preview.attackerState || !preview.effectPreview)
		return;
	auto & source = *preview.attackerState;
	const auto world = preview.effectPreview;
	const int remaining = std::max(0, action.movementBudget - action.movementCost);
	const auto * hero = world->battleGetOwnerHero(&source);
	const bool killed = action.anyKilled;
	const int pursuit = killed && hero && hero->hasActivePerk("new-horizons:offense", "new-horizons:offense.pursuit")
		? remaining : 0;
	auto luckySource = source.acquireState();
	luckySource->luckyOwnAttackSequence = action.luckyBefore || std::ranges::any_of(preview.fortuneStrikes,
		[&action](const auto & strike) { return strike.attackerId == action.actor && !strike.retaliation
			&& strike.resolvedLuck == ProjectedLuckOutcome::POSITIVE; });
	const int opportunist = newHorizonsOpportunist::movementAllowance(*world, luckySource.get(), remaining);
	const int vanish = vanishKill && source.alive() && source.canMove()
		&& !source.isTimeStopped() && !newHorizonsFrozen::isFrozen(source) && !source.armorerLastStandEndedActivation
		? newHorizonsShroud::vanishMovementAllowance(source.getMovementRange(0)) : 0;
	if(action.before.pending)
	{
		source.dreadCavalryState.retirePending();
		source.pursuitMovementRemaining = source.alive()
			? std::max(action.before.legacyTailAfterMovement(action.movementCost), std::max({pursuit, opportunist, vanish})) : 0;
	}
	else if(const auto granted = newHorizonsDreadCavalry::grantAfterMelee(*world, &source, action.firstVictim,
		action.controller, remaining, pursuit > 0 ? remaining : opportunist > 0 ? 2 : 0, vanish))
		source.dreadCavalryState = *granted;
}
ContinuationValue projectPending(const std::shared_ptr<HypotheticBattle> & world, uint32_t actor, DamageCache & cache)
{
	auto source = world->getForUpdate(actor);
	if(!source->dreadCavalryState.pending)
		return {};
	if(world->battleHasPendingDreadCavalry(source.get()))
	{
		PotentialTargets targets(source.get(), cache, world);
		if(!targets.possibleAttacks.empty() && targets.bestAction().attackValue() > 0)
		{
			BattleExchangeVariant exchange;
			const auto value = exchange.trackAttack(targets.bestAction(), world, cache);
			return {value, exchange.getScore().ourDamageReduce, exchange.getScore().enemyDamageReduce};
		}
	}
	source->dreadCavalryState.retirePending(); // NO_ACTION preserves the existing legacy movement tail.
	return {};
}
BattleAction chooseContinuation(const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & battle, const CStack * source)
{
	if(!environment || !battle->battleHasPendingDreadCavalry(source))
		return BattleAction::makeNoAction(source);
	auto model = std::make_shared<HypotheticBattle>(environment, battle);
	DamageCache cache;
	cache.buildDamageCache(model, battle->playerToSide(battle->battleGetActionController(source)));
	PotentialTargets targets(model->battleGetUnitByID(source->unitId()), cache, model);
	if(targets.possibleAttacks.empty() || targets.bestAction().attackValue() <= 0)
		return BattleAction::makeNoAction(source);
	const auto & best = targets.bestAction();
	return BattleAction::makeMeleeAttack(source, best.attack.defenderPos, best.from);
}
}
