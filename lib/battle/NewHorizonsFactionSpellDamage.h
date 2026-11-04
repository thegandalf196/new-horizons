/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "BattleSide.h"
#include "NewHorizonsOrdinaryStrike.h"
#include <optional>
#include <cstddef>
namespace battle { class Unit; }
class CGHeroInstance;
struct BattleAttackInfo;
namespace newHorizonsFactionSpellDamage
{
/// Per-recipient additive percentage; hero spells only. Creature magical abilities
/// and Cataclysm never enter this hook.
DLL_LINKAGE int bonusPercent(const CBattleInfoCallback & battle,
	const CGHeroInstance * hero, const battle::Unit * recipient);
DLL_LINKAGE Bonus exposedWeaknessMarker(BattleSide casterSide);
DLL_LINKAGE bool isExposedWeakness(const Bonus * bonus, BattleSide casterSide);
/// Capture geometry before the hit and publish the marker only after resolution.
DLL_LINKAGE newHorizonsOrdinaryStrike::EffectPlan exposedWeakness(
	const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
/// Genuine Rebirth outputs, including Chain and Phoenix Spark, carry this marker.
/// It is not a spell effect and survives ordinary Dispel/Purify.
DLL_LINKAGE Bonus rebirthOutputMarker();
DLL_LINKAGE bool isRebirthOutputMarker(const Bonus * bonus);
DLL_LINKAGE bool isRebirthOutput(const battle::Unit & unit);
DLL_LINKAGE int livingRebirthCount(const CBattleInfoCallback & battle, const CGHeroInstance & hero);
struct DLL_LINKAGE Cataclysm
{
	uint32_t source = 0;
	BattleSide side = BattleSide::NONE;
	int64_t maximumAggregateHP = 0;
	std::vector<uint32_t> targets;
};
DLL_LINKAGE std::optional<Cataclysm> captureCataclysm(
	const CBattleInfoCallback & battle, const battle::Unit & unit);
/// floor(20% of maximum HP), then equal integer shares; remainder discarded.
DLL_LINKAGE int64_t cataclysmShare(int64_t maximumAggregateHP, size_t targets);
/// Invincible adjacent targets retain their fixed pool share but take no damage.
/// Other recipients use the existing generic magical-ability mitigation once.
DLL_LINKAGE int64_t cataclysmDamage(const CBattleInfoCallback & battle,
	const battle::Unit & target, int64_t rawShare);
}
