/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "TownRecruitmentOffers.h"
#include "CGDwelling.h"
#include "CGTownInstance.h"
#include "CGHeroInstance.h"
#include "../CCreatureHandler.h"
#include "../entities/hero/NewHorizonsAdditionalSpecialties.h"
#include "../gameState/UpgradeInfo.h"

namespace townRecruitmentOffers
{
namespace
{
// Existing Necropolis Tomb of Souls / upgraded Tomb use dwelling level three.
constexpr size_t tombRow = 2;

bool residentSeth(const CGTownInstance & town, const CGHeroInstance * hero)
{
	return hero && hero->getOwner() == town.getOwner() && hero->getVisitedTown() == &town
		&& newHorizonsHeroes::hasAncientOaths(hero);
}

bool eligible(const CGTownInstance & town, PlayerColor requester)
{
	return requester.isValidPlayer() && town.getOwner() == requester
		&& town.getFactionID() == FactionID::NECROPOLIS
		&& (town.hasBuilt(BuildingID::DWELL_LVL_3) || town.hasBuilt(BuildingID::DWELL_LVL_3_UP))
		&& (residentSeth(town, town.getVisitingHero()) || residentSeth(town, town.getGarrisonHero()));
}

CreatureID alternative(CreatureID creature)
{
	if(creature == CreatureID(CreatureID::decode("core:wight")))
		return CreatureID(CreatureID::decode("core:mummy"));
	if(creature == CreatureID(CreatureID::decode("core:wraith")))
		return CreatureID(CreatureID::decode("new-horizons:royalMummy"));
	return CreatureID::NONE;
}
}

std::optional<RowOffer> query(const CGDwelling & source, size_t row, PlayerColor requester)
{
	if(row >= source.creatures.size())
		return std::nullopt;
	const auto & raw = source.creatures[row];
	RowOffer result{source.id, row, raw.first, raw.second};
	const auto * town = dynamic_cast<const CGTownInstance *>(&source);
	if(row != tombRow || !town || !eligible(*town, requester))
		return result;
	result.choices.clear();
	for(const auto creature : raw.second)
	{
		result.choices.push_back(creature);
		const auto other = alternative(creature);
		if(other.hasValue() && other.toCreature() && !vstd::contains(raw.second, other)
			&& !vstd::contains(result.choices, other))
			result.choices.push_back(other);
	}
	return result;
}
}

void CGTownInstance::fillUpgradeInfo(UpgradeInfo & info, const CStackInstance & stack) const
{
	for(const auto & dwelling : creatures)
		if(vstd::contains(dwelling.second, stack.getId()))
			for(const auto target : dwelling.second)
				if(vstd::contains(stack.getCreature()->upgrades, target))
					info.addUpgrade(target, stack.getType());

	if(stack.getId() != CreatureID(CreatureID::decode("core:mummy")) || !hasBuilt(BuildingID::DWELL_LVL_3_UP))
		return;
	const auto * army = stack.getArmy();
	if(!army || (army != this && army != getVisitingHero() && army != getGarrisonHero()))
		return;
	const auto royal = CreatureID(CreatureID::decode("new-horizons:royalMummy"));
	if(!royal.hasValue() || !vstd::contains(stack.getCreature()->upgrades, royal))
		return;
	constexpr size_t tombRow = 2;
	const auto offer = townRecruitmentOffers::query(*this, tombRow, army->getOwner());
	// A derived Wraith alternative requires neither base availability nor stock.
	if(offer && vstd::contains(creatures[tombRow].second, CreatureID(CreatureID::decode("core:wraith")))
		&& !vstd::contains(creatures[tombRow].second, royal) && vstd::contains(offer->choices, royal))
		info.addUpgrade(royal, stack.getType());
}
