/*
 * NewHorizonsMagicalAbilityDamage.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../Global.h"

#include <cstdint>

class CBattleInfoCallback;

namespace battle
{
class Unit;
}

namespace newHorizonsMagicalAbilityDamage
{
/// Applies saved New Horizons magical damage reduction to non-spell ability damage.
/// Spell resistance, spell-specific modifiers, and caster bonuses are intentionally excluded.
DLL_LINKAGE int64_t adjustDamage(const CBattleInfoCallback & battle, const battle::Unit & target, int64_t rawDamage);
}
