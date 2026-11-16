/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PriorityEvaluator.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CRewardableObject.h"
#include "../../../lib/rewardable/Interface.h"

namespace NK2AI
{
float RewardEvaluator::getVisitResourceValue(const CGObjectInstance * target, const CGHeroInstance * hero) const
{
	auto rewardable = dynamic_cast<const Rewardable::Interface *>(target);

	if(rewardable && hero)
	{
		auto resourceReward = 0.0f;

		for(int index : rewardable->getAvailableRewards(hero, Rewardable::EEventType::EVENT_FIRST_VISIT))
		{
			resourceReward += getCombinedResourceRequirementStrength(rewardable->configuration.info[index].reward.visitResources(hero, target->ID));
		}

		return resourceReward;
	}

	return 0;
}
}
