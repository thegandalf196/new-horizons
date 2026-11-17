/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include <cstdint>

namespace battle
{
/// Owned owner facts needed by native health arithmetic, not a saved-game DTO.
/// A forecast captures these once; no Unit, bonus provider or world is retained.
struct DLL_LINKAGE CHealthValueContext
{
	int32_t baseAmount = 0;
	int32_t maximumHealth = 1;

	void validate() const;
	bool operator==(const CHealthValueContext &) const = default;
};
}
