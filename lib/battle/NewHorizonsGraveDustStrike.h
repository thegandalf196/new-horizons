/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsOrdinaryStrike.h"
namespace newHorizonsGraveDustStrike
{
/// Ordinary primary Lich-family shots, including magical Death Cloud and genuine ranged reactions.
DLL_LINKAGE bool eligible(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
/// Caller supplies an actual standard body-loss receipt and authoritative body-replacement fact.
DLL_LINKAGE newHorizonsOrdinaryStrike::EffectPlan plan(const CBattleInfoCallback & battle,
	const BattleAttackInfo & attack, int64_t actualBodyHealthLoss, bool bodyReplaced);
}
