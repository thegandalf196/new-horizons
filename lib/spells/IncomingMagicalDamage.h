/*
 * IncomingMagicalDamage.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../../Global.h"
#include <cstdint>

namespace battle { class Unit; }
struct BattleAttackInfo;
struct DamageEstimation;
enum class SpellDamageElement : int8_t;
namespace spells
{
/// Nonnegative all-magical incoming vulnerability; exact spell subtypes are excluded.
DLL_LINKAGE int64_t incomingMagicalDamagePercent(const battle::Unit & target);
/// Apply once to already mitigated magical damage, before any final damage cap.
/// Physical callers must not use this function.
DLL_LINKAGE int64_t applyIncomingMagicalDamageModifier(const battle::Unit & target, int64_t damage);
/// Target elemental vulnerability, without spell/caster identity or all-magical scaling.
DLL_LINKAGE int64_t applyIncomingElementalDamageModifier(const battle::Unit & target, SpellDamageElement element, int64_t damage);
/// Intrinsic magical reduction and incoming vulnerability for otherwise raw abilities.
DLL_LINKAGE int64_t adjustUnmitigatedMagicalDamage(const battle::Unit & target, int64_t damage);
/// Final post-calculator magical creature stage; preserves physical results and damage caps.
DLL_LINKAGE void applyIncomingMagicalCreatureDamage(const BattleAttackInfo & attack, DamageEstimation & damage);
}
