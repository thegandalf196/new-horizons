/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleActivationPredicate.h"
#include "CBattleInfoCallback.h"
#include "HeroCommand.h"
#include "Unit.h"
namespace battle
{
bool isSecondWindRecipient(const HeroOrderState * order, const Unit & unit)
{
	return order && order->secondWindActive && order->primaryTargetUnitId == unit.unitId();
}
bool beginsActivation(const ActivationContext & context, const Unit * unit, BattleUnitTurnReason reason)
{
	if(reason == BattleUnitTurnReason::CRISIS_ORDER || reason == BattleUnitTurnReason::CRISIS_RESUME)
		return false;
	if(!unit || unit->isTimeStopped() || reason == BattleUnitTurnReason::ACTION_REJECTED
		|| reason == BattleUnitTurnReason::MASTER_GATE_CONTINUATION
		|| reason == BattleUnitTurnReason::PURSUIT_CONTINUATION
		|| reason == BattleUnitTurnReason::DREAD_CAVALRY_CONTINUATION
		|| reason == BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION
		|| reason == BattleUnitTurnReason::HERO_SPELLCAST || reason == BattleUnitTurnReason::UNIT_SPELLCAST)
		return false;
	if(reason != BattleUnitTurnReason::HERO_COMMAND)
		return true;
	const auto controllerSide = context.controllerSide(*unit);
	if(controllerSide != BattleSide::ATTACKER && controllerSide != BattleSide::DEFENDER)
		return false;
	return context.secondWindApplies(controllerSide, *unit);
}
bool beginsActivation(const CBattleInfoCallback & callback, const Unit * unit, BattleUnitTurnReason reason)
{
	class CallbackContext final : public ActivationContext
	{
		const CBattleInfoCallback & source;
	public:
		explicit CallbackContext(const CBattleInfoCallback & callback) : source(callback) {}
		BattleSide controllerSide(const Unit & current) const override
		{
			return source.playerToSide(source.battleGetOwner(&current));
		}
		bool secondWindApplies(BattleSide side, const Unit & current) const override
		{
			const auto order = source.battleGetHeroOrderState(side, HeroCommand::SECOND_WIND);
			return isSecondWindRecipient(order ? &*order : nullptr, current);
		}
	};
	const CallbackContext context(callback);
	return beginsActivation(context, unit, reason);
}
}
