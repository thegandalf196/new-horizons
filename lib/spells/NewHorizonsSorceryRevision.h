/*
 * NewHorizonsSorceryRevision.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../json/JsonNode.h"
#include <optional>
#include <string_view>

namespace newHorizonsSorceryRevision
{
struct FocusRules
{
	int32_t baseBasisPoints;
	int32_t powerBasisPoints;
	int32_t capBasisPoints;
};
DLL_LINKAGE int32_t slowBaseReduction(int32_t power, int32_t coefficient, int32_t warcasting, int32_t empower);
DLL_LINKAGE double scaleSlowVariantValue(double value, int32_t powerPercent);
DLL_LINKAGE int32_t focusMarkBasisPoints(const FocusRules & profile, int32_t power, int32_t coefficient,
	int32_t warcasting, int32_t empower, int32_t specialty);
DLL_LINKAGE int64_t livingArmorHitPoints(int64_t baseHitPoints, int64_t powerCoefficient,
	int32_t power, int32_t divisor, int32_t coefficient, int32_t warcasting, int32_t empower);
/// Absence preserves historical potency and initiative-only Slow.
DLL_LINKAGE std::optional<FocusRules> focusRules(const JsonNode & rules);
DLL_LINKAGE bool slowAffectsSpeed(const JsonNode & rules);
DLL_LINKAGE void validateSpellRow(const JsonNode & rules, std::string_view identity, const JsonNode & row);
DLL_LINKAGE void validateSerialization(const JsonNode & rules, bool supported);
}
