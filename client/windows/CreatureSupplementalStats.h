/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include <cstdint>
#include <optional>
#include <string>

class CStackInstance;
class CStack;
class CGHeroInstance;
class JsonNode;

namespace creatureSupplementalStats
{
struct Retaliations
{
	int32_t available = 0;
	int32_t total = 0;
	bool unlimited = false;
};

struct Readout
{
	Retaliations retaliations;
	std::optional<int> physicalReductionBasisPoints;
	int magicResistancePercent = 0;
};

/// Informational target-side baseline, not a hypothetical attack. Range- and
/// attacker-specific protection is excluded from the physical percentage.
Readout query(const CStackInstance & armyStack, const CStack * battleStack,
	const CGHeroInstance * hero, const JsonNode & magicRules);
std::string retaliationText(const Retaliations & value);
std::string percentageText(int basisPoints);
}
