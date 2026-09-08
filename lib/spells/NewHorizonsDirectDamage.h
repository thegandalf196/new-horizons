/*
 * NewHorizonsDirectDamage.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../json/JsonNode.h"
#include <cstdint>
#include <optional>

namespace newHorizonsMagic
{
constexpr int32_t MAX_DIRECT_DAMAGE_PARAMETER = 1000000;

/// Value copied from a saved spell record, not an installed spell definition.
/// Future primitive only: casting/AI integration and override precedence remain
/// the responsibility of the common mechanics entrypoint.
struct DLL_LINKAGE DirectDamageFormula
{
	int32_t base = 0;
	int32_t powerCoefficient = 0;

	int64_t evaluate(int32_t effectPower, int32_t divisor) const;
	bool operator==(const DirectDamageFormula &) const = default;
};

/// Validate only the optional directDamage field within a spell record. Outer
/// roster/schema validation remains separate. Present-null is malformed, not
/// absence. V1 rejects the field; v2 may omit it for legacy-effect spells.
DLL_LINKAGE std::optional<DirectDamageFormula> directDamageFormula(const JsonNode & spellRecord, int rulesetVersion);
}
