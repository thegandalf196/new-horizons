/*
 * Part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "EstatesState.h"

#include <algorithm>
#include <set>

namespace newHorizonsEstates
{
bool KingdomState::empty() const
{
	return stewards.empty() && std::all_of(recruitmentBaseCost.begin(), recruitmentBaseCost.end(),
		[](int64_t value) { return value == 0; });
}

void KingdomState::validate() const
{
	if(std::any_of(recruitmentBaseCost.begin(), recruitmentBaseCost.end(), [](int64_t value) { return value < 0; }))
		throw std::runtime_error("Negative Conscription cumulative price");
	std::set<ObjectInstanceID> appointed;
	for(const auto & [town, hero] : stewards)
		if(town.getNum() < 0 || hero.getNum() < 0 || town == hero || !appointed.insert(hero).second)
			throw std::runtime_error("Invalid Estates steward assignment");
}

void KingdomState::validateSerialization(bool supported) const
{
	validate();
	if(!supported && !empty())
		throw std::runtime_error("Estates kingdom state requires the revised Estates format");
}
}
