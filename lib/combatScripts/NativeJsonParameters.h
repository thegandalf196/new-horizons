/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../json/JsonNode.h"
#include "../bonuses/Bonus.h"

namespace nativeRules
{
/// Preserve the established captured-parameter wire format: numeric leaves are
/// doubles with game scope; containers have no scope. Empty tables are null.
/// This is a producer conversion, never a relaxation of saved-state validation.
DLL_LINKAGE JsonNode capturedParameters(const JsonNode &);
DLL_LINKAGE Bonus bonusFromDescriptor(const JsonNode &);
}
