/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PeerBonusDependencyClosure.h"
#include "Limiters.h"
#include "OwnedBonusPayloads.h"
#include <stdexcept>
#include <unordered_set>
namespace
{
bool output(const Bonus & bonus)
{
	return bonus.type == BonusType::SPELL_RESISTANCE_AURA || bonus.type == BonusType::HYPNOTIZED
		|| bonus.type == BonusType::CREATURE_ABILITY_SUPPRESSION;
}
std::vector<CSelector> dependencies(const Bonus & bonus)
{
	std::vector<CSelector> result;
	std::vector<TLimiterPtr> pending{bonus.limiter};
	std::unordered_set<const ILimiter *> visited;
	while(!pending.empty())
	{
		const auto limiter = pending.back(); pending.pop_back();
		if(!limiter || !visited.insert(limiter.get()).second) continue;
		if(const auto * group = dynamic_cast<const AggregateLimiter *>(limiter.get()))
			pending.insert(pending.end(), group->limiters.begin(), group->limiters.end());
		else if(const auto * another = dynamic_cast<const HasAnotherBonusLimiter *>(limiter.get()))
			result.push_back(another->dependencySelector());
		else if(dynamic_cast<const HasChargesLimiter *>(limiter.get()))
			result.emplace_back([sid = bonus.sid](const Bonus * value)
			{
				return value->type == BonusType::ARTIFACT_CHARGE && value->sid == sid;
			});
	}
	return result;
}
}
std::vector<BonusList> ownedRecipientPeer::dependencyClosure(const std::vector<BonusList> & local)
{
	std::vector<std::shared_ptr<const Bonus>> raw;
	for(const auto & node : local) for(const auto & bonus : node) raw.push_back(bonus);
	// Unknown modifiers may change output type; inspect ALL candidates before selection.
	ownedBonusPayloads::validateTypesAndTopology(raw);
	std::vector<std::shared_ptr<Bonus>> candidates, pending;
	std::unordered_set<const Bonus *> selected;
	for(const auto & node : local)
		for(const auto & bonus : node)
		{
			if(!bonus) throw std::invalid_argument("Null raw peer bonus input");
			candidates.push_back(bonus);
			if(output(*bonus) && selected.insert(bonus.get()).second) pending.push_back(bonus);
		}
	for(size_t index = 0; index < pending.size(); ++index)
		for(const auto & selector : dependencies(*pending[index]))
			for(const auto & candidate : candidates)
				if(selector(candidate.get()) && selected.insert(candidate.get()).second) pending.push_back(candidate);
	std::vector<BonusList> result(local.size());
	for(size_t node = 0; node < local.size(); ++node)
		for(const auto & bonus : local[node])
			if(selected.contains(bonus.get())) result[node].push_back(bonus);
	return result;
}
