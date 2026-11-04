/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsHandOfFateTargeting.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/Unit.h"
#include <stdexcept>
#include <limits>

namespace newHorizonsHandOfFate
{
battle::Target collateralPool(const CBattleInfoCallback & callback, uint32_t primaryUnitId)
{
	battle::Target result;
	for(const auto * unit : callback.battleGetUnitsIf([primaryUnitId](const battle::Unit * candidate)
	{
		return candidate && candidate->alive() && candidate->unitId() != primaryUnitId
			&& candidate->getPosition().isValid() && !candidate->isTurret();
	}))
		result.emplace_back(unit);
	return result;
}

uint64_t selectionWeight(uint32_t size, uint32_t hostileCount, bool recipientHostile, bool fateDealer)
{
	if(size == 0 || hostileCount > size)
		throw std::invalid_argument("Invalid Hand of Fate collateral pool");
	if(!fateDealer)
		return 1;
	const uint64_t friendly = size - hostileCount;
	return recipientHostile ? hostileCount + 2 * friendly : friendly;
}

uint64_t selectionDenominator(uint32_t size, bool fateDealer)
{
	return fateDealer ? static_cast<uint64_t>(size) * size : size;
}
std::pair<uint32_t, uint32_t> expectedManaFraction(bool primaryQualifies,
	uint64_t denominator, uint64_t legalWeight, uint64_t qualifiedLegalWeight)
{
	if(qualifiedLegalWeight > legalWeight || legalWeight > denominator)
		throw std::invalid_argument("Invalid prospective Hand of Fate recipient weights");
	if(denominator == 0)
		return {primaryQualifies ? 1U : 0U, 1};
	if(denominator > std::numeric_limits<uint32_t>::max() / 2)
		throw std::overflow_error("Hand of Fate Mana weights exceed supported battlefield size");
	const uint64_t total = 2 * denominator;
	const uint64_t qualifying = (primaryQualifies ? total - legalWeight : 0) + qualifiedLegalWeight;
	return {static_cast<uint32_t>(qualifying), static_cast<uint32_t>(total)};
}

}
