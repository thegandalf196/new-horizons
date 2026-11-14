/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../constants/EntityIdentifiers.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

class CGDwelling;

namespace townRecruitmentOffers
{
/// One source row and ONE stock count, shared by every legal species choice.
/// A derived view only: never append these alternatives to dwelling stock.
struct RowOffer
{
	ObjectInstanceID source;
	size_t row;
	uint32_t available;
	std::vector<CreatureID> choices;
};

/// Pure live-context query. An absent/invalid row returns no offer. Existing
/// ordinary choices remain legal, including authored/custom source rows.
DLL_LINKAGE std::optional<RowOffer> query(const CGDwelling & source, size_t row, PlayerColor requester);
}
