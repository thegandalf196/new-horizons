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
#include "../../lib/battle/IBattleInfoCallback.h"

namespace battleDamageValue
{
float averageOrderDamage(const DamageEstimation & damage)
{
	return static_cast<float>(std::max<int64_t>(0, damage.damage.min + damage.damage.max) / 2);
}
}
