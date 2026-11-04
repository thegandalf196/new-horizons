/*
 * BattleDamageValue.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleDamageValue.h"
#include "AttackPossibility.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleInfoCallback.h"

namespace battleDamageValue
{
float averageOrderDamage(const DamageEstimation & damage)
{
	return static_cast<float>(std::max<int64_t>(0, damage.damage.min + damage.damage.max) / 2);
}
}

battleDamageValue::IndirectDamageValue battleDamageValue::indirectDamageValue(const battle::Unit * referenceActor,
	const battle::Unit * beforeVictim, uint64_t damage, DamageCache & cache,
	const std::shared_ptr<CBattleInfoCallback> & state)
{
	const auto value = AttackPossibility::calculateDamageReduce(nullptr, beforeVictim, damage, cache, state);
	return {value, state->battleMatchOwner(referenceActor, beforeVictim, true)};
}

void battleDamageValue::addIndirectDamageValue(AttackPossibility & action, const battle::Unit * referenceActor,
	const battle::Unit * beforeVictim, int64_t damage, DamageCache & cache,
	const std::shared_ptr<CBattleInfoCallback> & state)
{
	if(!beforeVictim || damage <= 0) return;
	const auto value = indirectDamageValue(referenceActor, beforeVictim, static_cast<uint64_t>(damage), cache, state);
	(value.victimFriendly ? action.attackerDamageReduce : action.defenderDamageReduce) += value.amount;
}
