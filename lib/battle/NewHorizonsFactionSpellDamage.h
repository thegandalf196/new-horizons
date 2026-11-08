/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "BattleSide.h"
#include "NewHorizonsOrdinaryStrike.h"
namespace battle { class Unit; }
class CGHeroInstance;
struct BattleAttackInfo;
namespace newHorizonsFactionSpellDamage
{
/// Per-recipient Shroud percentage for hero spells. Conflux cast-time bonuses
/// belong to the accepted CastProfile, outside this recipient query.
DLL_LINKAGE int bonusPercent(const CBattleInfoCallback & battle,
	const CGHeroInstance * hero, const battle::Unit * recipient);
DLL_LINKAGE Bonus exposedWeaknessMarker(BattleSide casterSide);
DLL_LINKAGE bool isExposedWeakness(const Bonus * bonus, BattleSide casterSide);
/// Capture geometry before the hit and publish the marker only after resolution.
DLL_LINKAGE newHorizonsOrdinaryStrike::EffectPlan exposedWeakness(
	const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
}
