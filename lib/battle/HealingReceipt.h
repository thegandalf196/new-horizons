/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <cstdint>
class IBonusBearer;
namespace battle
{
/// Reduce eligible, already capped body HP once. Prevented HP rounds down.
/// This neither probes capacity nor changes health/provenance or producer budgets.
/// Strongest eligible incoming coefficient; never sums separate sources.
DLL_LINKAGE int32_t healingReceivedReduction(const IBonusBearer & bearer);
DLL_LINKAGE int64_t receivedHealing(int64_t eligibleHealth, int32_t reductionBasisPoints);
}
