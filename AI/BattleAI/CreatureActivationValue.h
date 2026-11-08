/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <memory>
class DamageCache;
class HypotheticBattle;
class CBattleInfoCallback;
struct ReachabilityInfo;
namespace battle { class Unit; }
namespace creatureActivationValue
{
/// Existing full-HP Cure recovery lower bound; reuses the ordinary legal-action query.
float bestLegalActionValue(const battle::Unit *, DamageCache &, const std::shared_ptr<HypotheticBattle> &);
/// Existing nominal single-activation pressure, in common damage-reduction units.
/// No pathfinding, actions, resource spending, random draws or continuation search.
/// Terminal passive comparisons can opt into current allegiance after mind control;
/// existing spell/control callers retain their original side-based convention.
float expectedTargetActivationValue(const battle::Unit * target, DamageCache & damageCache,
	const std::shared_ptr<CBattleInfoCallback> & projectedBattle, bool effectiveAllegiance = false,
	const ReachabilityInfo * completeDistances = nullptr, int movementRadius = -1);
}
