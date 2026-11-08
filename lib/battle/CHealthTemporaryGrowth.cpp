/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CUnitState.h"
#include <limits>
namespace battle
{
bool CHealth::appendTemporaryCreatures(int32_t count)
{
	if(count <= 0 || getCount() <= 0 || shadowGiftMaximumHealthLost > 0
		|| count > std::numeric_limits<int32_t>::max() - getCount()
		|| count > std::numeric_limits<int32_t>::max() - resurrected)
		return false;
	ensureCasualtyProvenanceLedger();
	// Append full members. OVERHEAL would heal existing wounded capacity
	// cohorts first and is therefore not a count-growth operation.
	fullUnits += count; // The total-count bound above also bounds this component.
	addResurrected(count);
	// These members were created, not restored from a permanent casualty.
	// Existing corpse cohorts and permanent casualty debt remain unchanged.
	validateCasualtyProvenanceLedger();
	return true;
}
}
