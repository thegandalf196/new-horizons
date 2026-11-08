/* Part of VCMI engine. GPL-2.0-or-later; see license.txt/AUTHORS. */
#pragma once
#include "../../../Global.h"
#include <array>
class JsonNode;

namespace newHorizonsHeroes
{
/// One synchronous native hero query, never retained across gameplay changes.
/// Performs the original first full validation and owns only its admitted rank tables.
/// Public standalone capability arithmetic remains independently validating.
class DLL_LINKAGE SiegeCapabilityRead final
{
	int multiplier = 1;
	int version = 0;
	std::array<int, 4> ratings{};
	std::array<int, 4> controls{};
	int rankValue(const std::array<int, 4> &, int rank, const char * key) const;
public:
	SiegeCapabilityRead(const JsonNode & resolvedRules, int artilleryRank);
	int ballistaMultiplier() const { return multiplier; }
	int siegeRating(int rank) const;
	int directControlChance(int rank) const;
};
}
