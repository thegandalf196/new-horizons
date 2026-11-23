/*
 * Part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "CGTownInstance.h"
#include "CGHeroInstance.h"
#include "NewHorizonsEstates.h"
#include "NewHorizonsEstatesPolicy.h"
#include "EstatesStewardship.h"
#include "../CPlayerState.h"
#include "../GameLibrary.h"
#include "../StartInfo.h"
#include "../callback/IGameInfoCallback.h"
#include "../entities/ResourceTypeHandler.h"
#include "../entities/building/CBuilding.h"
#include "../entities/faction/CTownHandler.h"

int32_t CGTownInstance::getStewardGoldBeforeHandicap(const CGHeroInstance * excluded) const
{
	if(!getOwner().isValidPlayer())
		return 0;
	if(const auto * steward = newHorizonsEstates::townSteward(*this))
		return steward == excluded ? 0 : newHorizonsEstates::stewardGold(*steward);
	constexpr int32_t goldPerHero = 250;
	const auto qualifies = [this, excluded](const CGHeroInstance * hero)
	{
		return hero && hero != excluded && hero->getVisitedTown() == this && hero->getOwner() == getOwner()
			&& !newHorizonsEstates::usesRevisedPolicy(hero->getPerkState().getRules())
			&& hero->hasActivePerk("new-horizons:estates", "new-horizons:estates.stewardship");
	};
	const auto * visitor = getVisitingHero();
	const auto * garrison = getGarrisonHero();
	return (qualifies(visitor) ? goldPerHero : 0)
		+ (garrison != visitor && qualifies(garrison) ? goldPerHero : 0);
}

TResources CGTownInstance::dailyIncome() const
{
	ResourceSet ret;

	for (GameResID k : LIBRARY->resourceTypeHandler->getAllObjects())
		ret[k] += valOfBonuses(BonusType::GENERATE_RESOURCE, BonusSubtypeID(k));

	for(const auto & p : getTown()->buildings)
	{
		BuildingID buildingUpgrade;

		for(const auto & p2 : getTown()->buildings)
		{
			if (p2.second->upgrade == p.first)
			{
				buildingUpgrade = p2.first;
			}
		}
		if (!hasBuilt(buildingUpgrade)&&(hasBuilt(p.first)))
		{
			ret += p.second->produce;
		}
	}

	if (!getOwner().isValidPlayer())
		return ret;

	ret[EGameResID::GOLD] += getStewardGoldBeforeHandicap();
	const auto & playerSettings = cb->getPlayerSettings(getOwner());
	ret.applyHandicap(playerSettings->handicap.percentIncome);
	return ret;
}


int CGTownInstance::getMarketEfficiency() const
{
	if(!hasBuiltResourceMarketplace())
		return 0;

	const PlayerState *p = cb->getPlayerState(tempOwner);
	assert(p);

	int marketCount = p->valOfBonuses(BonusType::MARKETPLACE_ACCESS);
	const auto hasMerchantPrince = [this](const CGHeroInstance * hero)
	{
		return hero && hero->getVisitedTown() == this && hero->getOwner() == getOwner()
			&& !newHorizonsEstates::usesRevisedPolicy(hero->getPerkState().getRules())
			&& hero->hasActivePerk("new-horizons:estates", "new-horizons:estates.merchantPrince");
	};
	// The town receives two virtual Marketplaces, not two per resident holder.
	// Keep the ordinary Marketplace gate and every consuming offer's own cap.
	const int legacyBonus = hasMerchantPrince(getVisitingHero()) || hasMerchantPrince(getGarrisonHero()) ? 2 : 0;
	marketCount += std::max(legacyBonus, newHorizonsEstates::marketplaceBonus(*p));

	return marketCount;
}

double CGTownInstance::getResourceExchangeEffectiveness(GameResID sell, GameResID buy) const
{
	return IMarket::getResourceExchangeEffectiveness(sell, buy);
}

TResources CGTownInstance::getBuildingCost(const BuildingID & buildingID) const
{
	if (vstd::contains(getTown()->buildings, buildingID))
		return newHorizonsEstates::buildingCost(getTown()->buildings.at(buildingID)->resources,
			newHorizonsEstates::townHasPerk(*this, "new-horizons:estates.architect"));
	else
	{
		logGlobal->error("Town %s at %s has no possible building %d!", getNameTextID(), anchorPos().toString(), buildingID.toEnum());
		return TResources();
	}

}
