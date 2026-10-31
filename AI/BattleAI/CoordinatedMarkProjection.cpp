/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CoordinatedMarkProjection.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include "../../lib/battle/NewHorizonsCoordinatedMarks.h"
#include "../../lib/battle/MeleeReturnDestination.h"
#include "../../lib/battle/GateDrawbridgeFootprint.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/entities/building/TownFortifications.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/BonusList.h"
namespace coordinatedMarkAI
{
namespace
{
bool commitLanding(HypotheticBattle & branch, uint32_t id, BattleHex destination)
{
	auto actor = branch.getForUpdate(id);
	if(!actor || !actor->alive() || actor->getPosition() == destination || actor->armorerLastStandEndedActivation
		|| !actor->hasBonusOfType(BonusType::RETURN_AFTER_STRIKE)
		|| actor->hasBonusOfType(BonusType::NOT_ACTIVE) || actor->hasBonusOfType(BonusType::BIND_EFFECT))
		return false;
	const auto gate = branch.battleGetGateState();
	if(branch.battleGetFortifications().wallsHealth > 0 && actor->unitSide() == BattleSide::DEFENDER
		&& gate != EGateState::DESTROYED && gate != EGateState::BLOCKED && gate != EGateState::OPENED)
	{
		const bool wideMoat = std::ranges::any_of(branch.battleGetAllObstaclesOnPos(BattleHex(BattleHex::GATE_BRIDGE), false),
			[](const auto & obstacle) { return obstacle->obstacleType == CObstacleInstance::MOAT; });
		if(gateDrawbridgeFootprintContains(*actor, destination, wideMoat))
			branch.openGateForReturn();
	}
	branch.moveUnit(id, destination);
	const bool committed = actor->getPosition() == destination;
	if(committed)
		branch.evaluateLandingObstacleTriggersForUnit(*branch.getServerCallback(), *actor);
	return committed; // Landing death/displacement cannot undo the committed endpoint.
}
void publish(HypotheticBattle & branch, const newHorizonsOrdinaryStrike::EffectPlan & plan)
{
	const auto * current = branch.battleGetUnitByID(plan.target);
	if(!current || (plan.add.empty() && plan.remove.empty()))
		return;
	auto target = branch.getForUpdate(plan.target);
	target->removeUnitBonus(plan.remove);
	if(target->alive() && !target->isGhost() && !target->isTimeStopped())
		target->addUnitBonus(plan.add);
}
void valueLanding(AttackPossibility & preview,
	const std::vector<std::shared_ptr<StackWithBonuses>> & before, DamageCache & cache)
{
	for(const auto & old : before)
	{
		const auto * current = preview.effectPreview->battleGetUnitByID(old->unitId());
		if(!current) continue;
		const auto loss = std::max<int64_t>(0, old->getAvailableHealth() - current->getAvailableHealth());
		if(loss == 0) continue;
		preview.returnLandingHealthLoss[old->unitId()] = loss;
		const auto value = AttackPossibility::calculateDamageReduce(nullptr, old.get(), loss, cache, preview.effectPreview);
		if(preview.effectPreview->battleMatchActionController(preview.attack.attacker, current, true))
			preview.attackerDamageReduce += value;
		else
			preview.defenderDamageReduce += value;
	}
}
}
void prepareReturn(AttackPossibility & preview, int movementBudget, DamageCache & cache)
{
	if(preview.coordinatedReturnHandled || !preview.effectPreview || !preview.attackerState || preview.attack.shooting || preview.attack.retaliation || !preview.attack.physicalDamage)
		return;
	auto & branch = *preview.effectPreview;
	auto actor = branch.getForUpdate(preview.attackerState->unitId());
	if(!newHorizonsCoordinatedMarks::canMarkReturn(branch, *actor) || !actor->alive()
		|| actor->armorerLastStandEndedActivation || !actor->hasBonusOfType(BonusType::RETURN_AFTER_STRIKE)
		|| actor->hasBonusOfType(BonusType::NOT_ACTIVE) || actor->hasBonusOfType(BonusType::BIND_EFFECT))
		return;
	preview.coordinatedReturnHandled = true;
	const auto origin = preview.validatedVoluntaryApproach ? preview.validatedApproachOrigin : preview.attack.attacker->getPosition();
	if(!preview.from.isValid() || origin == actor->getPosition())
		return;
	const auto destination = battleMovement::meleeReturnDestination(branch, *actor, origin,
		std::min<int>(actor->getMovementRange(), movementBudget));
	if(!destination)
		return;
	for(auto strike = preview.fortuneStrikes.rbegin(); strike != preview.fortuneStrikes.rend(); ++strike)
	{
		if(strike->attackerId != actor->unitId() || strike->shooting || strike->retaliation
			|| strike->bracePreemptive || strike->cleaveDamagePercent != 0 || strike->hits.empty())
			continue;
		const auto * victim = branch.battleGetUnitByID(strike->defenderId);
		if(!victim) return;
		const auto effect = *destination == origin ? newHorizonsCoordinatedMarks::prepareReturn(branch, *actor, *victim)
			: newHorizonsOrdinaryStrike::EffectPlan{};
		std::vector<std::shared_ptr<StackWithBonuses>> before;
		for(const auto * unit : branch.battleAliveUnits())
		{
			auto snapshot = std::make_shared<StackWithBonuses>(&branch, unit);
			snapshot->freezeDamageScoringBonusSnapshot();
			before.push_back(std::move(snapshot));
		}
		if(commitLanding(branch, actor->unitId(), *destination))
		{
			preview.completedReturnDestination = *destination;
			preview.preparedReturnMark = effect;
			valueLanding(preview, before, cache);
		}
		preview.attackerState = actor;
		return;
	}
}
void completeReturn(AttackPossibility & preview)
{
	if(preview.completedReturnMarkTarget || !preview.effectPreview || !preview.completedReturnDestination)
		return;
	publish(*preview.effectPreview, preview.preparedReturnMark);
	const auto * target = preview.effectPreview->battleGetUnitByID(preview.preparedReturnMark.target);
	if(target && !preview.preparedReturnMark.add.empty() && target->alive() && !target->isGhost() && !target->isTimeStopped())
		preview.completedReturnMarkTarget = target->unitId();
}
bool replayMovement(const AttackPossibility & captured, HypotheticBattle & destination, BattleHex origin)
{
	const auto expectedOrigin = captured.validatedVoluntaryApproach ? captured.validatedApproachOrigin
		: captured.attack.attacker->getPosition();
	return captured.completedReturnDestination && expectedOrigin == origin
		&& captured.attackerState && commitLanding(destination, captured.attackerState->unitId(), *captured.completedReturnDestination);
}

void replayReturn(const AttackPossibility & captured, HypotheticBattle & destination, BattleHex origin)
{
	if(captured.completedReturnDestination && *captured.completedReturnDestination == origin)
		publish(destination, captured.preparedReturnMark);
}

}
