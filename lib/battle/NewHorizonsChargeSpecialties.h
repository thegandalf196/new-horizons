/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsOrdinaryStrike.h"
class CBattleInfoCallback;
struct BattleAttackInfo;
namespace battle { class Unit; }
namespace newHorizonsChargeSpecialties
{
/// Cheap source/family/native-trait admission to reuse existing ordinary movement preview.
/// This does not establish movement or grant a benefit, and performs no path search.
DLL_LINKAGE bool requiresChargeMovementPreview(const CBattleInfoCallback & battle,
	const battle::Unit & actor, const battle::Unit & defender);
DLL_LINKAGE int defenseFromCharge(int actualHexes, int defensePerHex, int maximumDefense);
DLL_LINKAGE bool isLanceFormation(const Bonus * bonus);
/// Prepare from validated prehit facts; publish only after a real primary blow completes.
/// The zero-damage case qualifies. Publication still rejects a dead/ghost/stopped actor.
DLL_LINKAGE newHorizonsOrdinaryStrike::EffectPlan lanceFormation(const CBattleInfoCallback & battle,
	const BattleAttackInfo & attack);
/// Current native FLYING plus exact validated attack-local voluntary-flight distance are required.
DLL_LINKAGE int stormWingsDefenseIgnorePercent(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
}
