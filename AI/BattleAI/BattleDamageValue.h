/*
 * BattleDamageValue.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../lib/constants/EntityIdentifiers.h"
#include <memory>
class CBattleInfoCallback;
class DamageCache;
class AttackPossibility;
namespace battle { class Unit; }
struct DamageEstimation;

namespace battleDamageValue
{
struct IndirectDamageValue { float amount; bool victimFriendly; };
void addIndirectDamageValue(AttackPossibility &, const battle::Unit * referenceActor,
	const battle::Unit * beforeVictim, int64_t damage, DamageCache &,
	const std::shared_ptr<CBattleInfoCallback> &);
/// Existing signed exchange valuation of an already resolved indirect injury.
IndirectDamageValue indirectDamageValue(const battle::Unit * referenceActor, const battle::Unit * beforeVictim,
	uint64_t damage, DamageCache &, const std::shared_ptr<CBattleInfoCallback> &);

/// Historical nonnegative integer midpoint, then float conversion. Shared by
/// direct Order estimates, Blink and generic forecasts; not a utility converter.
float averageOrderDamage(const DamageEstimation & damage);
}
