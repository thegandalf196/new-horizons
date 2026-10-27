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
/// Absence preserves historical potency and initiative-only Slow.
DLL_LINKAGE std::optional<FocusRules> focusRules(const JsonNode & rules);
DLL_LINKAGE bool slowAffectsSpeed(const JsonNode & rules);
DLL_LINKAGE void validateSpellRow(const JsonNode & rules, std::string_view identity, const JsonNode & row);
DLL_LINKAGE void validateSerialization(const JsonNode & rules, bool supported);
}
