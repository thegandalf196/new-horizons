/*
 * NewHorizonsTimedUtility, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../../Global.h"
#include <cstdint>
#include <optional>
class CSpell;
class CGHeroInstance;
namespace spells { class Mechanics; }
namespace battle { class Unit; }
namespace newHorizonsMagic
{
inline constexpr int32_t HOLY_ARMOR_MAXIMUM_REDUCTION = 60;
inline constexpr int32_t HEAVENLY_GALE_MAXIMUM_REDUCTION_BASIS_POINTS = 8000;
/// Native Light-enchantment duration, including Crusade's literal base and The Chosen's cap.
DLL_LINKAGE int32_t timedLightEffectDuration(const spells::Mechanics & mechanics);
/// Exact authored fixed-protection transition. nullopt means unsupported producer;
/// callers must not infer a refresh benefit from a generic enchantment timer.
DLL_LINKAGE std::optional<bool> usefulTimedLightProtection(const spells::Mechanics&, const battle::Unit&);
/// Configured fixed MDR source from the spell's actual mastery definition.
/// Ambiguous/multiple or school-specific sources have no single book value.
DLL_LINKAGE std::optional<int32_t> configuredHolyArmorBase(const CSpell & spell, int32_t effectLevel);
DLL_LINKAGE int64_t heavenlyGaleReductionBasisPoints(int32_t power, int32_t coefficient, int32_t empower, bool aegis, int32_t crownAndAltar = 0);
DLL_LINKAGE int64_t holyArmorReduction(int32_t configuredBase, int32_t power, int32_t coefficient, int32_t empower, bool aegis, int32_t crownAndAltar = 0);
DLL_LINKAGE int32_t forgetfulnessBaseDuration(int32_t power, int32_t coefficient, int32_t empower);
DLL_LINKAGE int32_t landMineCountForHero(int32_t power, int32_t coefficient, const CGHeroInstance * hero);
/// Authored Hex damage-share component, shared by authority and caster preview.
DLL_LINKAGE int32_t hexOfPainDamageSharePercent();
/// The exact common saturation and extension order used by BaseMechanics.
DLL_LINKAGE int32_t durationWithCastExtensions(int32_t base, bool extend, bool echoedDuration);
}
