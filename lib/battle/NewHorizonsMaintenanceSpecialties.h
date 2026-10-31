/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "BattleUnitTurnReason.h"
class CBattleInfoCallback;
struct Bonus;
namespace battle { class Unit; }
namespace newHorizonsMaintenanceSpecialties
{
enum class Trigger { VOLUNTARY_DEFEND, ACTIVATION_START };
DLL_LINKAGE int woundHealing(const CBattleInfoCallback & battle, const battle::Unit & unit, Trigger trigger);
DLL_LINKAGE int activationHealing(const CBattleInfoCallback & battle, const battle::Unit & unit, BattleUnitTurnReason reason);
DLL_LINKAGE bool isDefendRetaliationBonus(const Bonus * bonus);
DLL_LINKAGE int defendRetaliations(const battle::Unit & unit);
}
