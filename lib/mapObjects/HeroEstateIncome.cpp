/*
 * Part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "CGHeroInstance.h"
#include "../CPlayerState.h"
#include "../GameLibrary.h"
#include "../entities/ResourceTypeHandler.h"
#include "../callback/IGameInfoCallback.h"
#include "../StartInfo.h"

ResourceSet CGHeroInstance::dailyIncome() const
{
	ResourceSet income;

	for (GameResID k : LIBRARY->resourceTypeHandler->getAllObjects())
		income[k] += valOfBonuses(BonusType::GENERATE_RESOURCE, BonusSubtypeID(k));

	if(hasActivePerk("new-horizons:estates", "new-horizons:estates.taxCollector"))
	{
		static constexpr int goldPerTown = 100;
		static constexpr int maximumDailyGold = 500;
		const auto * ownerState = cb->getPlayerState(getOwner());
		if(ownerState)
		{
			const auto townsForThisPerk = std::min(ownerState->getTowns().size(),
				static_cast<size_t>(maximumDailyGold / goldPerTown));
			income[EGameResID::GOLD] += static_cast<int>(townsForThisPerk) * goldPerTown;
		}
	}
	if(hasActivePerk("new-horizons:estates", "new-horizons:estates.prosperity"))
	{
		++income[EGameResID::WOOD];
		++income[EGameResID::ORE];
	}
	if(hasActivePerk("new-horizons:estates", "new-horizons:estates.alchemist"))
		for(const auto resource : {EGameResID::MERCURY, EGameResID::SULFUR, EGameResID::CRYSTAL, EGameResID::GEMS})
			++income[resource];

	const auto & playerSettings = cb->getPlayerSettings(getOwner());
	income.applyHandicap(playerSettings->handicap.percentIncome);
	return income;
}
