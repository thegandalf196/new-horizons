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
namespace newHorizonsMagic
{
/// Configured fixed MDR source from the spell's actual mastery definition.
/// Ambiguous/multiple or school-specific sources have no single book value.
DLL_LINKAGE std::optional<int32_t> configuredHolyArmorBase(const CSpell & spell, int32_t effectLevel);
DLL_LINKAGE int64_t heavenlyGaleReductionBasisPoints(int32_t power, int32_t coefficient,
	int32_t warcasting, int32_t empower, bool aegis, int32_t crownAndAltar = 0);
DLL_LINKAGE int64_t holyArmorReduction(int32_t configuredBase, int32_t power, int32_t coefficient,
	int32_t warcasting, int32_t empower, bool aegis, int32_t crownAndAltar = 0);
DLL_LINKAGE int32_t forgetfulnessBaseDuration(int32_t power, int32_t coefficient,
	int32_t warcasting, int32_t empower);
DLL_LINKAGE int32_t landMineCountForHero(int32_t power, int32_t coefficient, const CGHeroInstance * hero);
/// Authored Hex damage-share component, shared by authority and caster preview.
DLL_LINKAGE int32_t hexOfPainDamageSharePercent();
/// The exact common saturation and extension order used by BaseMechanics.
DLL_LINKAGE int32_t durationWithCastExtensions(int32_t base, bool extend, bool echoedDuration);
}
