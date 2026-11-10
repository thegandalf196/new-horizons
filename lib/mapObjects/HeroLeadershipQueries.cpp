/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CGHeroInstance.h"
#include "../battle/NewHorizonsCommandResources.h"
#include "../entities/hero/NewHorizonsLeadership.h"
#include "../entities/hero/NewHorizonsCapabilityRules.h"
#include "army/CStackInstance.h"

std::optional<newHorizonsHeroes::LeadershipCapacity> CGHeroInstance::getLeadershipCapacity() const
{
	return getLeadershipCapacity(*this);
}

std::optional<newHorizonsHeroes::LeadershipCapacity> CGHeroInstance::getLeadershipCapacity(const CCreatureSet & army) const
{
	if(!newHorizonsHeroes::usesLegacyLeadership(capabilityRules))
		return std::nullopt;
	const int trainedLeadership = std::max(0, valOfBonuses(BonusType::LEADERSHIP));
	if(capabilityRules["rulesetVersion"].Integer() >= 2)
		return newHorizonsHeroes::LeadershipCapacity{
			newHorizonsCommand::effectiveLeadership(*this,
				static_cast<int64_t>(newHorizonsHeroes::capabilityLeadershipRating(capabilityRules, level)) + trainedLeadership), 0, 100};
	uint64_t used = 0;
	// Version 1 counts adventure army creatures, including undead, one per unit.
	// Commanders and artifact machines are not roster slots; battle-only arrivals
	// do not rewrite the adventure army. Splitting a stack cannot change usage.
	for(const auto & [slot, stack] : army.Slots())
	{
		if(stack->getCount() < 0)
			throw std::runtime_error("Leadership requires resolved adventure army counts");
		used += static_cast<uint64_t>(stack->getCount());
	}
	auto result = newHorizonsHeroes::capabilityLeadership(capabilityRules, level,
		getSecSkillLevel(SecondarySkill::LEADERSHIP), used);
	result.capacity = newHorizonsCommand::effectiveLeadership(*this, result.capacity + trainedLeadership);
	return result;
}

std::optional<newHorizonsHeroes::LeadershipSlotCapacity> CGHeroInstance::getLeadershipSlotCapacity(CreatureID creature) const
{
	return newHorizonsHeroes::heroLeadershipSlotCapacity(*this, creature);
}
