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
/// Native Fire immunity for nonspell effects. ANY school immunity is not a Fire source.
DLL_LINKAGE bool fireImmune(const battle::Unit & target);
/// Target-only Fire damage in a captured multiplicative-MDR context; unsupported positive damage throws.
/// One union of Fire/ANY reductions, no cast or caster bonus. Owner admission must validate dependency.
DLL_LINKAGE int64_t adjustFireDamage(const CBattleInfoCallback & battle, const battle::Unit & target, int64_t rawDamage);
DLL_LINKAGE int64_t adjustDamage(const CBattleInfoCallback & battle, const battle::Unit & target, int64_t rawDamage);
}
