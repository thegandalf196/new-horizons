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

struct DamageEstimation;

namespace battleDamageValue
{
/// Historical nonnegative integer midpoint, then float conversion. Shared by
/// direct Order estimates, Blink and generic forecasts; not a utility converter.
float averageOrderDamage(const DamageEstimation & damage);
}
