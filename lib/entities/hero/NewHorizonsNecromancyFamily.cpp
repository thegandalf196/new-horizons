/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsNecromancyPolicy.h"
#include "../../CCreatureHandler.h"
#include <stdexcept>
namespace newHorizonsNecromancy
{
namespace
{
template<typename Membership>
bool skeletonFamily(CreatureID creature, const Membership & membership)
{
	if(!creature.hasValue()) return false;
	const CreatureID skeleton(CreatureID::SKELETON);
	if(creature == skeleton) return true;
	return membership(skeleton, creature);
}
}
bool isSkeletonFamily(CreatureID creature)
{
	return skeletonFamily(creature, [](CreatureID skeleton, CreatureID target)
	{
		const auto * base = skeleton.toCreature();
		return base && base->upgrades.contains(target);
	});
}
bool isSkeletonFamily(CreatureID creature, const std::optional<std::set<CreatureID>> & upgrades)
{
	return skeletonFamily(creature, [&upgrades](CreatureID, CreatureID target)
	{
		if(!upgrades) throw std::logic_error("Owned Skeleton upgrade membership is unknown");
		return upgrades->contains(target);
	});
}
}
