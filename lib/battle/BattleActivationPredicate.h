/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include <cstdint>
#include "BattleSide.h"
#include "BattleUnitTurnReason.h"
class CBattleInfoCallback;
struct HeroOrderState;
namespace battle
{
class Unit;
/// Numeric current-context queries only. The engine owns admission and the
/// current controller/Order state; unsupported domains throw, not guessed false.
class DLL_LINKAGE ActivationContext
{
public:
	virtual ~ActivationContext() = default;
	virtual BattleSide controllerSide(const Unit & unit) const = 0;
	virtual bool secondWindApplies(BattleSide side, const Unit & unit) const = 0;
};
/// Pure canonical tail; unitId is queried only for a present active Order.
DLL_LINKAGE bool isSecondWindRecipient(const HeroOrderState * order, const Unit & unit);
DLL_LINKAGE bool beginsActivation(const ActivationContext & context, const Unit * unit, BattleUnitTurnReason reason);
DLL_LINKAGE bool beginsActivation(const CBattleInfoCallback & callback, const Unit * unit, BattleUnitTurnReason reason);
}
