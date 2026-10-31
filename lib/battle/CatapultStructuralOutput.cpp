/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CBattleInfoCallback.h"
#include "NewHorizonsMachineSpecialties.h"
#include "Unit.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../entities/hero/NewHorizonsCapabilityRules.h"
#include <algorithm>
#include <limits>
namespace
{
int64_t scaledBattleOutput(int64_t base, int32_t outputPercent)
{
	if(base <= 0 || outputPercent <= 0)
		return 0;
	return (base / 100) * outputPercent + ((base % 100) * outputPercent) / 100;
}
}
int32_t CBattleInfoCallback::battleGetCatapultStructuralDamage(
	const battle::Unit * attacker, int32_t hitQuality) const
{
	// This callback is called at the catapult boundary, where the machine type
	// is already known. Some legacy adapters provide a lightweight Unit proxy
	// without creature identity, so do not rediscover its type here.
	if(!attacker || hitQuality <= 0)
		return 0;

	int64_t structuralOutput = std::min<int32_t>(hitQuality, 2);
	if(const auto * hero = battleGetOwnerHero(attacker);
		hero && hero->getCapabilityRules()["rulesetVersion"].Integer() >= 3)
	{
		if(const auto siege = hero->getSiegeCapabilities())
			structuralOutput *= std::max(0, newHorizonsMachineSpecialties::catapultStructuralOutput(
				*hero, *attacker, siege->siegeRating, siege->catapultStructuralDamage));
	}
	structuralOutput = scaledBattleOutput(structuralOutput, battleGetActivationOutputPercent(attacker));
	return static_cast<int32_t>(std::clamp<int64_t>(structuralOutput, 0,
		std::numeric_limits<int32_t>::max()));
}
