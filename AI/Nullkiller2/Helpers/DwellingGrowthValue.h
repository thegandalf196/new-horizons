/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#pragma once
#include "../../../lib/constants/EntityIdentifiers.h"
#include <cstdint>
class CCallback;
class CGObjectInstance;
namespace NK2AI
{
/// Existing capture value policy, using the dwelling's canonical weekly production.
DLL_EXPORT uint64_t getDwellingArmyGrowth(CCallback * cb, const CGObjectInstance * target, PlayerColor myColor);
}
