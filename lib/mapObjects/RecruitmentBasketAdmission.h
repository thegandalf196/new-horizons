/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#pragma once

#include "../constants/EntityIdentifiers.h"
#include <span>

class CArmedInstance;

namespace recruitmentBasket
{
struct Row
{
	CreatureID creature;
	int32_t count = 0;
};

/// Read-only placement preflight in the actual submission order. Reuses the
/// ordinary army admission rules and preserves seven native army slots.
DLL_LINKAGE bool fits(const CArmedInstance & destination, std::span<const Row> rows);
}
