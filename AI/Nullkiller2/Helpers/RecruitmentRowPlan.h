/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../AIUtility.h"
#include "../../../lib/entities/hero/NewHorizonsCommandArmy.h"
#include <optional>
#include <set>

class CCreatureSet;
class CGHeroInstance;
class CGDwelling;
namespace townRecruitmentOffers { struct RowOffer; }

namespace NK2AI
{
struct RecruitmentProjection
{
	std::vector<newHorizonsHeroes::RecruitmentSlot> slots;
	std::set<SlotID> disbanded;
};
struct RecruitmentRowPlan
{
	creInfo creature;
	RecruitmentProjection projection;
	TResources cost;
};

/// Compare alternatives against the SAME stock/resources/army, then return one
/// allocation. The caller commits only that allocation before the next row.
std::optional<RecruitmentRowPlan> planRecruitmentRow(const CCreatureSet & army,
	const CGHeroInstance * carrier, const CGDwelling & source,
	const townRecruitmentOffers::RowOffer & offer, int available,
	const TResources & resources, const RecruitmentProjection & projection,
	bool allowDisband = true);
}
