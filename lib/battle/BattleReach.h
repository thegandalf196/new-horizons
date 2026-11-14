/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <cstdint>

class IBonusBearer;

namespace battleReach
{
/// Maximum occupied-hex distance: ordinary adjacency plus the net Reach bonus.
/// Shared by authoritative legality, previews and presentation; no unit-ID rules.
DLL_LINKAGE int64_t maximumMeleeDistance(const IBonusBearer & bearer);
/// Formatter/modifier counterpart; netGap is the already summed raw bonus.
DLL_LINKAGE int64_t maximumMeleeDistance(int64_t netGap);
}
