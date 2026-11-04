/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include "../battle/Destination.h"
#include <cstdint>
#include <utility>

class CBattleInfoCallback;
namespace newHorizonsHandOfFate
{
/// Exact native draw pool, BEFORE immunity/resistance checks. Stable callback
/// order is preserved so sharing this query does not change RNG consumption.
DLL_LINKAGE battle::Target collateralPool(const CBattleInfoCallback & battle, uint32_t primaryUnitId);
/// Uniform weight=1/size; Fate Dealer uses its existing two-draw/fair-tie rule.
DLL_LINKAGE uint64_t selectionWeight(uint32_t size, uint32_t hostileCount,
	bool recipientHostile, bool fateDealer);
DLL_LINKAGE uint64_t selectionDenominator(uint32_t size, bool fateDealer);
/// Expected outcome fraction, not ratio of expected target counts. Blocked
/// collateral leaves a primary-only outcome while retaining its native probability.
/// Returns {qualifyingWeight, totalWeight}; empty collateral uses {primary,1}.
DLL_LINKAGE std::pair<uint32_t, uint32_t> expectedManaFraction(bool primaryQualifies,
	uint64_t denominator, uint64_t legalWeight, uint64_t qualifiedLegalWeight);
}
