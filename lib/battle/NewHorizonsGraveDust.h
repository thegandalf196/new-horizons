/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../bonuses/Bonus.h"
#include <vector>
class IBonusBearer;
namespace battle { class Unit; }
namespace physicalAfflictions { struct MarkerMetadata; }
namespace newHorizonsGraveDust
{
DLL_LINKAGE bool isReservedSource(const Bonus & bonus);
DLL_LINKAGE void validateParsedMarker(const Bonus & bonus, const physicalAfflictions::MarkerMetadata & metadata);
/// Validate exact reserved records; unrelated historical records are unchanged.
DLL_LINKAGE bool validateRecord(const Bonus & bonus);
DLL_LINKAGE bool isEffect(const Bonus * bonus);
DLL_LINKAGE void validateBearerGroup(const IBonusBearer & bearer);
/// Nonwriting packet-record preflight; legacy batch topology checks stay with PhysicalAffliction.
DLL_LINKAGE void validateBatchRecords(const std::vector<Bonus> & removed,
	const std::vector<std::vector<Bonus> *> & incoming);
/// Complete installed source group; individual Bonus descriptors need not own the companion.
DLL_LINKAGE void validateSourceGroup(const std::vector<Bonus> & bonuses);
/// Positive-only unstacked validation prevents stacking from concealing duplicate companions.
DLL_LINKAGE void validateUnitGroup(const battle::Unit & unit);
DLL_LINKAGE std::vector<Bonus> records(int32_t reductionBasisPoints, PlayerColor sourcePlayer);
}
