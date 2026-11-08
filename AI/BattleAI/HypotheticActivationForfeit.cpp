/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "ActivationForfeit.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/battle/NewHorizonsPlague.h"

bool HypotheticBattle::wasCurrentActivationForfeited(uint32_t unitId) const
{
	const auto found = forfeitedActivations.find(unitId);
	return found != forfeitedActivations.end() && found->second.round == projectedRound;
}
bool HypotheticBattle::currentForfeitCompleted(uint32_t unitId) const
{
	const auto found = forfeitedActivations.find(unitId);
	return wasCurrentActivationForfeited(unitId) && found->second.completed;
}
void HypotheticBattle::nextTurnPreservingForfeit(uint32_t unitId, BattleUnitTurnReason reason,
	HypotheticLifecycleTiming * timing)
{
	if(usesOwnedFrameQueries()) throw std::logic_error("Owned lifecycle needs its captured transaction");
	const auto * unit = battleGetUnitByID(unitId);
	if(!unit || !activationForfeit::pending(*this, *unit, reason))
		throw std::invalid_argument("Deferred forfeiture requires an actual incapacitated activation");
	nextTurnImpl(unitId, reason, true, timing);
}
void HypotheticBattle::completeForfeitedActivation(uint32_t unitId)
{
	if(!wasCurrentActivationForfeited(unitId) || currentForfeitCompleted(unitId)) return;
	forfeitedActivations.at(unitId).completed = true;
	auto unit = getForUpdate(unitId);
	auto statuses = forfeitedActivations.at(unitId).reason == BattleUnitTurnReason::TURN_QUEUE
		? newHorizonsFrozen::removalPlan(*unit) : std::vector<Bonus>{};
	const auto stun = newHorizonsStun::removalPlan(*unit);
	unit->removeUnitBonus(CSelector([](const Bonus * bonus)
	{
		return Bonus::UntilActivationEnds(bonus) && !newHorizonsOffense::isNoQuarterMoralePenalty(bonus);
	}));
	unit->consumeNoQuarterActivation();
	newHorizonsPlague::completeActivation(*this, *getServerCallback(), unitId);
	unit = getForUpdate(unitId);
	if(!unit->isTimeStopped())
	{
		if(unit->alive() && !unit->isGhost()) unit->removeUnitBonus(statuses);
		unit->removeUnitBonus(stun); // An actually consumed slot cannot survive death as pending Stun.
	}
	const auto side = playerToSide(battleGetOwner(unit.get()));
	if(side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
		if(auto order = getHeroOrderState(side, HeroCommand::SECOND_WIND);
			order && order->secondWindActive && order->primaryTargetUnitId == unitId)
		{
			order->secondWindActive = false;
			setHeroOrderState(side, *order);
		}
	endFortuneActivation();
	completeNormalActivation(unitId);
}

void HypotheticBattle::finishProjectedActivation(uint32_t unitId)
{
	if(currentForfeitCompleted(unitId)) return;
	endFortuneActivation();
	completeNormalActivation(unitId);
}
