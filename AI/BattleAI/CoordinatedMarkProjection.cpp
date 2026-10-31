/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CoordinatedMarkProjection.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include "../../lib/battle/NewHorizonsCoordinatedMarks.h"
#include "../../lib/battle/MeleeReturnDestination.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/BonusList.h"
namespace coordinatedMarkAI
{
void completeReturn(AttackPossibility & preview)
{
	if(!preview.effectPreview || !preview.attackerState || preview.attack.shooting || preview.attack.retaliation || !preview.attack.physicalDamage)
		return;
	auto & branch = *preview.effectPreview;
	auto actor = branch.getForUpdate(preview.attackerState->unitId());
	if(!newHorizonsCoordinatedMarks::canMarkReturn(branch, *actor) || !actor->alive()
		|| actor->armorerLastStandEndedActivation || !actor->hasBonusOfType(BonusType::RETURN_AFTER_STRIKE)
		|| actor->hasBonusOfType(BonusType::NOT_ACTIVE) || actor->hasBonusOfType(BonusType::BIND_EFFECT))
		return;
	const auto origin = preview.attack.attacker->getPosition();
	if(!preview.from.isValid() || origin == actor->getPosition())
		return;
	const auto destination = battleMovement::meleeReturnDestination(branch, *actor, origin, actor->getMovementRange());
	if(!destination || *destination != origin)
		return;
	for(auto strike = preview.fortuneStrikes.rbegin(); strike != preview.fortuneStrikes.rend(); ++strike)
	{
		if(strike->attackerId != actor->unitId() || strike->shooting || strike->retaliation
			|| strike->bracePreemptive || strike->cleaveDamagePercent != 0 || strike->hits.empty())
			continue;
		const auto * victim = branch.battleGetUnitByID(strike->defenderId);
		if(!victim)
			return;
		const auto effect = newHorizonsCoordinatedMarks::prepareReturn(branch, *actor, *victim);
		actor->setPosition(origin); // Same return forecast as selected replay, with the shared legal destination.
		if(!effect.add.empty())
		{
			auto target = branch.getForUpdate(effect.target);
			target->removeUnitBonus(effect.remove);
			target->addUnitBonus(effect.add);
			preview.completedReturnMarkTarget = effect.target;
		}
		preview.attackerState = actor;
		return;
	}
}
void replayReturn(const AttackPossibility & captured, HypotheticBattle & destination, BattleHex origin)
{
	if(!captured.completedReturnMarkTarget || !captured.effectPreview || !captured.attackerState
		|| captured.attackerState->getPosition() != origin)
		return;
	const auto * actor = destination.battleGetUnitByID(captured.attackerState->unitId());
	const auto * final = captured.effectPreview->battleGetUnitByID(*captured.completedReturnMarkTarget);
	const auto * current = destination.battleGetUnitByID(*captured.completedReturnMarkTarget);
	if(!actor || actor->getPosition() != origin || !final || !current || !current->alive()
		|| current->isGhost() || current->isTimeStopped())
		return;
	const CSelector identity([](const Bonus * bonus)
	{
		return newHorizonsCoordinatedMarks::isMark(bonus) && bonus->statusIdentity == "featheredFeint";
	});
	const auto bonuses = final->getUnstackedBonuses(identity);
	for(const auto & bonus : *bonuses)
		if(static_cast<uint32_t>(bonus->parameters->toVector()[1]) == actor->unitId() && bonus->turnsRemain > 0)
		{
			auto target = destination.getForUpdate(current->unitId());
			target->removeUnitBonus(identity);
			target->addUnitBonus(std::vector<Bonus>{*bonus});
			return;
		}
}

}
