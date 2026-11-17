/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "MechanicsPresence.h"
#include "../bonuses/Bonus.h"
#include "../bonuses/Updaters.h"
#include "../CStack.h"
#include "../bonuses/BonusGraphQuery.h"
#include <unordered_set>

namespace
{
bool preservesType(const TUpdaterPtr & updater, unsigned depth = 0) noexcept
{
	if(!updater) return true;
	if(depth >= 32) return false; // Cyclic/deep composite extensions remain unknown.
	const auto & type = typeid(*updater);
	// Exact types only: an extension may override createUpdatedBonus and change
	// its output type. These native implementations only edit value/owner.
	if(type == typeid(IUpdater) || type == typeid(GrowsWithLevelUpdater)
		|| type == typeid(TimesHeroLevelUpdater) || type == typeid(TimesStackSizeUpdater)
		|| type == typeid(TimesArmySizeUpdater) || type == typeid(TimesStackLevelUpdater)
		|| type == typeid(DivideStackLevelUpdater) || type == typeid(TimesHeroLevelDivideStackLevelUpdater)
		|| type == typeid(OwnerUpdater)) return true;
	if(type != typeid(CompositeUpdater)) return false;
	for(const auto & part : static_cast<const CompositeUpdater &>(*updater).updaters)
		if(!preservesType(part, depth + 1)) return false;
	return true;
}
}
namespace battle
{
void MechanicsPresence::include(const Bonus & bonus) noexcept
{
	const auto index = static_cast<size_t>(bonus.type);
	if(index >= count || !preservesType(bonus.updater) || !preservesType(bonus.propagationUpdater))
		unknown = true;
	else
		types.set(index);
}

MechanicsPresence nativeMechanicsPresence(const CBonusSystemNode & node)
{
	MechanicsPresence result;
	const auto & graph = liveBonusGraph();
	std::vector<const CBonusSystemNode *> pending{&node};
	std::unordered_set<const CBonusSystemNode *> seen;
	while(!pending.empty())
	{
		const auto * current = pending.back(); pending.pop_back();
		if(!seen.insert(current).second) continue;
		for(const auto & record : graph.local(*current)) result.include(*record);
		const auto & parents = graph.parents(*current);
		pending.insert(pending.end(), parents.begin(), parents.end());
	}
	return result;
}
}

bool CStack::mayHaveBonusType(BonusType type) const
{
	if(type == BonusType::MORALE && veteranCohesionEarned) return true;
	// Transformed/custom stacks may supply sources outside the ordinary graph.
	return typeid(*this) != typeid(CStack) || hasBattleFormState()
		|| cachedMayHaveBonusType(type);
}
