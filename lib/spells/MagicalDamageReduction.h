/*
 * MagicalDamageReduction.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "../../Global.h"
#include "../battle/BattleSide.h"

#include <cstdint>
#include <optional>
#include <vector>

class JsonNode;
class IBattleInfo;

namespace spells
{
struct DLL_LINKAGE CapturedOverwhelmingFormula
{
	BattleSide side;
	uint64_t token = 0;

	bool operator==(const CapturedOverwhelmingFormula &) const = default;
};

/// Both metadata fields must be valid. Tokens use canonical decimal strings
/// so Lua's floating-point number representation cannot lose cast identity.
DLL_LINKAGE std::optional<CapturedOverwhelmingFormula> capturedOverwhelmingFormula(const JsonNode & captured);
/// Formula contributes 50% dynamically only when the battle admits its saved
/// token. Missing context retains the historical captured-array behavior.
/// Malformed ordinary contributors retain their fail-closed behavior; invalid
/// Formula metadata contributes no 50% while valid ordinary sources remain.
DLL_LINKAGE std::vector<int> capturedMdrPenetrations(const JsonNode & captured, uint32_t targetUnitId,
	const IBattleInfo * battle = nullptr);
struct DLL_LINKAGE MagicalDamageReductionResult
{
	int64_t damageWithoutPenetration = 0;
	int64_t damageWithPenetration = 0;

	bool operator==(const MagicalDamageReductionResult &) const = default;
};

/// Applies independent magical-damage reductions and optional relative
/// penetration to raw damage. Independent reductions multiply, aggregate MDR
/// is capped at 95%, and penetration reduces that capped aggregate rather than
/// subtracting percentage points. Both returned damage values are floored once
/// after the complete product is evaluated.
DLL_LINKAGE MagicalDamageReductionResult calculateMagicalDamageReduction(
	int64_t rawDamage, const std::vector<int> & independentReductionsPercent, int penetrationPercent);

/// Basis-point counterpart. Use this when a reduction source carries fractional
/// percentage points, such as Iron Discipline's captured magical reduction.
DLL_LINKAGE MagicalDamageReductionResult calculateMagicalDamageReductionBasisPoints(
	int64_t rawDamage, const std::vector<int> & independentReductionsBasisPoints, int penetrationPercent);

/// Independent penetration sources multiply their remaining fractions exactly;
/// damage is floored only after both reduction and penetration products.
DLL_LINKAGE MagicalDamageReductionResult calculateMagicalDamageReduction(
	int64_t rawDamage, const std::vector<int> & independentReductionsPercent,
	const std::vector<int> & independentPenetrationsPercent);
DLL_LINKAGE MagicalDamageReductionResult calculateMagicalDamageReductionBasisPoints(
	int64_t rawDamage, const std::vector<int> & independentReductionsBasisPoints,
	const std::vector<int> & independentPenetrationsPercent);
}
