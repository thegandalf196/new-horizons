/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedFrameCaptureFacts.h"
#include "../bonuses/BonusParameters.h"
#include "../bonuses/Limiters.h"
#include <unordered_set>
namespace battle::ownedFrameCapture
{
Requirements requirements(const std::vector<BonusList> & lists)
{
	std::vector<std::shared_ptr<const Bonus>> values;
	for(const auto & list : lists) for(const auto & value : list) values.push_back(value);
	ownedBonusPayloads::validateTypesAndTopology(values); // Existing finite type/DAG admission, no duplicate registry.
	Requirements result; std::unordered_set<const Bonus *> bonuses; std::unordered_set<const ILimiter *> seen;
	for(size_t i = 0; i < values.size(); ++i)
	{
		const auto value = values[i]; if(!bonuses.insert(value.get()).second) continue;
		if(value->source == BonusSource::SECONDARY_SKILL) result.secondaryRecords.push_back(value);
		if(const auto * parameters = value->parameters ? value->parameters->combatEffects() : nullptr)
			for(const auto & effect : parameters->effects)
				if(const auto * child = std::get_if<BonusParametersOnCombatEvent::CombatEffectBonus>(&effect); child && child->bonus)
					values.push_back(child->bonus);
		std::vector<TLimiterPtr> pending{value->limiter};
		while(!pending.empty())
		{
			const auto limiter = pending.back(); pending.pop_back();
			if(!limiter || !seen.insert(limiter.get()).second) continue;
			if(const auto * group = dynamic_cast<const AggregateLimiter *>(limiter.get()))
				pending.insert(pending.end(), group->limiters.begin(), group->limiters.end());
			if(const auto * creature = dynamic_cast<const CCreatureTypeLimiter *>(limiter.get()))
				if(creature->includeUpgrades) result.families.insert(creature->creatureID);
			if(dynamic_cast<const TerrainLimiter *>(limiter.get())) result.terrain = true;
			if(const auto * faction = dynamic_cast<const FactionLimiter *>(limiter.get());
				faction && !faction->faction.hasValue() && value->source == BonusSource::CREATURE_ABILITY)
				result.sourceCreatures.insert(value->sid.as<CreatureID>());
			if(const auto * adjacent = dynamic_cast<const UnitAdjacentLimiter *>(limiter.get()))
			{
				result.adjacency = true;
				if(adjacent->filter.includeUpgrades && !adjacent->filter.anyCreature)
					result.adjacentFamilies.insert(adjacent->targetUnit);
			}
		}
	}
	return result;
}
}
