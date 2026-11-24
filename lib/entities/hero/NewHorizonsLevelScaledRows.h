/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsCastleAdventureSpecialties.h"
#include "NewHorizonsRuleValidation.h"

/// Shared reading and validation of level-scaled specialty rows {base, perLevel, maximum}.
/// One definition for every faction's specialty rules: same-named private helpers in
/// separate sources collide when CI batches them into one Unity translation unit.
namespace newHorizonsHeroes::detail
{
inline LevelScaledValue scaledRow(const JsonNode & row, const char * base, const char * perLevel, const char * maximum)
{
	return {static_cast<int>(row[base].Integer()), static_cast<int>(row[perLevel].Integer()),
		static_cast<int>(row[maximum].Integer())};
}

/// Base and maximum in [1, limit], per-level growth in [0, limit], maximum >= base.
inline void requireScaledRow(const JsonNode & row, const char * base, const char * perLevel, const char * maximum,
	int limit, const std::string & name)
{
	require(integer(row[base], 1, limit) && integer(row[perLevel], 0, limit)
		&& integer(row[maximum], 1, limit) && row[maximum].Integer() >= row[base].Integer(), name);
}
}
