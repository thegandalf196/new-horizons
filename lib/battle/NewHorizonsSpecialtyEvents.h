/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsOrdinaryStrike.h"
class CBattleInfoCallback;
struct BattleAttackInfo;
namespace battle { class Unit; }
namespace newHorizonsSpecialtyEvents
{
DLL_LINKAGE bool isUndertowDefense(const Bonus * bonus);
DLL_LINKAGE bool isGoblinFrenzySpent(const Bonus * bonus);
DLL_LINKAGE bool isReceipt(const Bonus * bonus);
DLL_LINKAGE bool needsStrikeEvents(const CBattleInfoCallback & battle, const battle::Unit * actor, const battle::Unit * target);
DLL_LINKAGE int firstStrikeFinalMultiplier(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
/// Only invoke after a real accepted primary hit; a zero-damage hit still qualifies.
DLL_LINKAGE std::vector<newHorizonsOrdinaryStrike::EffectPlan> acceptedStrike(
	const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
DLL_LINKAGE bool requiresTravelReceipt(const CBattleInfoCallback & battle, const battle::Unit & actor);
DLL_LINKAGE int retaliationReduction(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
DLL_LINKAGE int sorshaReduction(const CBattleInfoCallback & battle, const battle::Unit & actor);
}
