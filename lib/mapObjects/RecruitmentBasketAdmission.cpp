/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#include "StdInc.h"
#include "RecruitmentBasketAdmission.h"
#include "CGHeroInstance.h"
#include "../CCreatureHandler.h"
#include "../entities/hero/NewHorizonsCommandArmy.h"
#include "../entities/hero/NewHorizonsLeadership.h"

namespace recruitmentBasket
{
bool fits(const CArmedInstance & destination, std::span<const Row> rows)
{
	auto projected = newHorizonsHeroes::commandArmySlots(destination);
	const auto * hero = dynamic_cast<const CGHeroInstance *>(&destination);
	for(const auto & row : rows)
	{
		if(row.count == 0)
			continue;
		const auto * definition = row.creature.toCreature();
		if(row.count < 0 || !definition || definition->warMachine != ArtifactID::NONE)
			return false; // Quick recruitment is an army basket, not an artifact storefront.
		if(hero && hero->getArmyCommand())
		{
			const auto plan = newHorizonsHeroes::planCommandRecruitment(*hero, projected, row.creature, row.count);
			if(!plan)
				return false;
			projected = plan->slots;
			continue;
		}
		const auto capacity = hero ? hero->getLeadershipSlotCapacity(row.creature) : std::nullopt;
		const auto slot = newHorizonsHeroes::recruitmentSlot(projected, row.creature, row.count,
			capacity ? std::optional<int64_t>(capacity->maximum) : std::nullopt);
		if(!slot.validSlot())
			return false;
		const auto existing = std::ranges::find(projected, slot, &newHorizonsHeroes::RecruitmentSlot::slot);
		if(existing == projected.end())
			projected.push_back({slot, row.creature, row.count});
		else
			existing->count += row.count;
	}
	return true;
}
}
