/*
 * NewHorizonsSorceryRevision.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../json/JsonNode.h"
#include "../constants/EntityIdentifiers.h"
#include <string_view>
class CGHeroInstance;

namespace newHorizonsSorceryRevision
{
DLL_LINKAGE int32_t slowBaseReduction(int32_t power, int32_t coefficient, int32_t empower);
DLL_LINKAGE double scaleSlowVariantValue(double value, int32_t powerPercent);
/// Called once after caster/recipient specialties and before mass normalization.
DLL_LINKAGE double finalSlowValue(double value, int32_t powerPercent, bool chronomancer);
DLL_LINKAGE bool hasChronomancer(const CGHeroInstance * hero);
DLL_LINKAGE int32_t spellbinderBonus(const CGHeroInstance * hero, SpellID spell);
DLL_LINKAGE int32_t focusAdditionalRangedPercent(const CGHeroInstance * hero, SpellID spell);
DLL_LINKAGE int64_t livingArmorHitPoints(int64_t baseHitPoints, int64_t powerCoefficient,
	int32_t power, int32_t divisor, int32_t coefficient, int32_t empower);
/// Absence preserves historical potency and initiative-only Slow.
DLL_LINKAGE bool usesFocusEnchantment(const JsonNode & rules);
DLL_LINKAGE bool slowAffectsSpeed(const JsonNode & rules);
DLL_LINKAGE void validateSpellRow(const JsonNode & rules, std::string_view identity, const JsonNode & row);
DLL_LINKAGE void validateSerialization(const JsonNode & rules, bool supported);
}
