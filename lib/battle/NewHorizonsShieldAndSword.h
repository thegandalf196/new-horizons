/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsOrdinaryStrike.h"
class CBattleInfoCallback;
namespace battle { class Unit; }
namespace newHorizonsShieldAndSword
{
DLL_LINKAGE bool isProtection(const Bonus * bonus);
DLL_LINKAGE bool eligible(const CBattleInfoCallback & battle, const battle::Unit & actor);
/// Caller supplies a completed, validated hostile offensive melee sequence, never an individual strike.
DLL_LINKAGE newHorizonsOrdinaryStrike::EffectPlan completion(const CBattleInfoCallback & battle,
	const battle::Unit & actor, bool completedPrimaryMelee);
}
