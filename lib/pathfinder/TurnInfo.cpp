/*
 * TurnInfo.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "TurnInfo.h"

#include "../IGameSettings.h"
#include "../TerrainHandler.h"
#include "../GameLibrary.h"
#include "../bonuses/BonusList.h"
#include "../callback/IGameInfoCallback.h"
#include "../json/JsonNode.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../mapObjects/MiscObjects.h"
#include "NewHorizonsMovement.h"
#include "NewHorizonsMovementBonuses.h"


TConstBonusListPtr TurnInfoBonusList::getBonusList(const CGHeroInstance * target, const CSelector & bonusSelector)
{
	std::lock_guard guard(bonusListMutex);

	if (target->getTreeVersion() == bonusListVersion)
		return bonusList;

	bonusList = target->getBonuses(bonusSelector);
	bonusListVersion = target->getTreeVersion();

	return bonusList;
}

int TurnInfo::hasWaterWalking() const
{
	return waterWalkingTest;
}

int TurnInfo::hasFlyingMovement() const
{
	return flyingMovementTest;
}

int TurnInfo::hasNoTerrainPenalty(const TerrainId &terrain) const
{
	return noterrainPenalty[terrain.num];
}

int TurnInfo::hasFreeShipBoarding() const
{
	return freeShipBoardingTest;
}

int TurnInfo::getFlyingMovementValue() const
{
	return flyingMovementValue;
}

int TurnInfo::getWaterWalkingValue() const
{
	return waterWalkingValue;
}

int TurnInfo::getRoughTerrainDiscountValue() const
{
	return roughTerrainDiscountValue;
}

int TurnInfo::getMovementCostBase() const
{
	return moveCostBaseValue;
}

int TurnInfo::getMovePointsLimitLand() const
{
	return movePointsLimitLand;
}

int TurnInfo::getMovePointsLimitWater() const
{
	return movePointsLimitWater;
}

int TurnInfo::getMovePointsLimitAir() const
{
	return movePointsLimitAir;
}

bool TurnInfo::usesNewHorizonsMovement() const
{
	return target->usesNewHorizonsMovement();
}

void TurnInfo::initializeMovementLimits(TurnInfoCache * sharedCache, const int turn, const CSelector & daySelector)
{
	// Airship movement has its own unchanged limit and no land/sea modifiers.
	movePointsLimitAir = 2000;
	if(target->usesNewHorizonsMovement())
	{
		static const CSelector landSelector = Selector::typeSubtype(BonusType::MOVEMENT, BonusCustomSubtype::heroMovementLand);
		static const CSelector seaSelector = Selector::typeSubtype(BonusType::MOVEMENT, BonusCustomSubtype::heroMovementSea);
		const auto limits = newHorizonsMovement::dailyMovementLimits(*target,
			sharedCache->movementPointsLimitLand.getBonusList(target, landSelector),
			sharedCache->movementPointsLimitWater.getBonusList(target, seaSelector), turn);
		movePointsLimitLand = limits.land.limit;
		movePointsLimitWater = limits.sea.limit;
		lighthouseSeaMovePoints = limits.lighthouseDeparture.limit;
		return;
	}

	int lowestSpeed;
	if(target->getTreeVersion() == sharedCache->heroLowestSpeedVersion)
		lowestSpeed = sharedCache->heroLowestSpeedValue;
	else
	{
		lowestSpeed = target->getLowestCreatureSpeed();
		sharedCache->heroLowestSpeedValue = lowestSpeed;
		sharedCache->heroLowestSpeedVersion = target->getTreeVersion();
	}
	{
		static const CSelector selector = Selector::typeSubtype(BonusType::MOVEMENT, BonusCustomSubtype::heroMovementSea);
		const auto & values = target->cb->getSettings().getValue(EGameSettings::HEROES_MOVEMENT_POINTS_SEA).Vector();
		const auto bonuses = sharedCache->movementPointsLimitWater.getBonusList(target, selector);
		const int base = lowestSpeed < values.size() ? values[lowestSpeed].Integer() : values.back().Integer();
		movePointsLimitWater = bonuses->valOfBonuses(daySelector, base);
	}
	{
		static const CSelector selector = Selector::typeSubtype(BonusType::MOVEMENT, BonusCustomSubtype::heroMovementLand);
		const auto & values = target->cb->getSettings().getValue(EGameSettings::HEROES_MOVEMENT_POINTS_LAND).Vector();
		const auto bonuses = sharedCache->movementPointsLimitLand.getBonusList(target, selector);
		const int base = lowestSpeed < values.size() ? values[lowestSpeed].Integer() : values.back().Integer();
		movePointsLimitLand = bonuses->valOfBonuses(daySelector, base);
	}
}

TurnInfo::TurnInfo(TurnInfoCache * sharedCache, const CGHeroInstance * target, int Turn,
	const CCreatureSet * projectedArmy)
	: target(target)
	, noterrainPenalty(LIBRARY->terrainTypeHandler->size())
{
	CSelector daySelector = Selector::days(Turn);
	const bool newHorizonsMovement = target->usesNewHorizonsMovement();
	newHorizonsPathfinding = newHorizonsMovement
		&& target->hasActivePerk("new-horizons:logistics", "new-horizons:logistics.pathfinding");
	newHorizonsNavigation = newHorizonsMovement
		&& target->hasActivePerk("new-horizons:logistics", "new-horizons:logistics.navigation");
	newHorizonsRapidEmbarkation = newHorizonsMovement
		&& target->hasActivePerk("new-horizons:logistics", "new-horizons:logistics.rapidEmbarkation");
	newHorizonsRoadmaster = newHorizonsMovement
		&& target->hasActivePerk("new-horizons:logistics", "new-horizons:logistics.roadmaster");
	newHorizonsWayfarer = newHorizonsMovement
		&& target->hasActivePerk("new-horizons:logistics", "new-horizons:logistics.wayfarer");
	const bool newHorizonsMountaineer = newHorizonsMovement
		&& target->hasActivePerk("new-horizons:logistics", "new-horizons:logistics.mountaineer");

	{
		static const CSelector selector = Selector::type()(BonusType::WATER_WALKING);
		const auto & bonuses = sharedCache->waterWalking.getBonusList(target, selector);
		const CSelector applicableBonus = daySelector.And([target](const Bonus * bonus)
		{
			return !target->isNewHorizonsAdventureMovementArtifactBonus(*bonus);
		});
		waterWalkingTest = bonuses->getFirst(applicableBonus) != nullptr;
		waterWalkingValue = bonuses->valOfBonuses(applicableBonus);
	}

	{
		static const CSelector selector = Selector::type()(BonusType::FLYING_MOVEMENT);
		const auto & bonuses = sharedCache->flyingMovement.getBonusList(target, selector);
		const CSelector applicableBonus = daySelector.And([target](const Bonus * bonus)
		{
			return !target->isNewHorizonsAdventureMovementArtifactBonus(*bonus);
		});
		flyingMovementTest = bonuses->getFirst(applicableBonus) != nullptr;
		flyingMovementValue = bonuses->valOfBonuses(applicableBonus);
	}

	{
		static const CSelector selector = Selector::type()(BonusType::FREE_SHIP_BOARDING);
		const auto & bonuses = sharedCache->freeShipBoarding.getBonusList(target, selector);
		freeShipBoardingTest = bonuses->getFirst(daySelector) != nullptr;
	}

	{
		static const CSelector selector = Selector::type()(BonusType::ROUGH_TERRAIN_DISCOUNT);
		const auto & bonuses = sharedCache->roughTerrainDiscount.getBonusList(target, selector);
		roughTerrainDiscountValue = bonuses->valOfBonuses(daySelector);
	}

	{
		static const CSelector selector = Selector::type()(BonusType::BASE_TILE_MOVEMENT_COST);
		const auto & bonuses = sharedCache->baseTileMovementCost.getBonusList(target, selector);
		int baseMovementCost = target->cb->getSettings().getInteger(EGameSettings::HEROES_MOVEMENT_COST_BASE);
		moveCostBaseValue = bonuses->valOfBonuses(daySelector, baseMovementCost);
	}

	initializeMovementLimits(sharedCache, Turn, daySelector);

	{
		static const CSelector selector = Selector::type()(BonusType::NO_TERRAIN_PENALTY);
		const auto & bonuses = sharedCache->noTerrainPenalty.getBonusList(target, selector);
		for (const auto & bonus : *bonuses)
		{
			TerrainId affectedTerrain = bonus->subtype.as<TerrainId>();
			noterrainPenalty.at(affectedTerrain.num) = true;
		}
		if(newHorizonsMountaineer)
		{
			noterrainPenalty.at(TerrainId::ROUGH) = true;
			noterrainPenalty.at(TerrainId::SUBTERRANEAN) = true;
		}

		const auto allStacksNativeForTerrain = [this](TerrainId terrainId)
		{
			for (const auto & slot : this->target->Slots())
			{
				if (!slot.second->isNativeTerrain(terrainId))
					return false;
			}
			return true;
		};

		for (const auto & terrain : LIBRARY->terrainTypeHandler->objects)
		{
			auto terrainId = terrain->getId();
			const bool native = newHorizonsMovement
				? target->hasNewHorizonsTerrainAffinity(terrainId, projectedArmy)
				: allStacksNativeForTerrain(terrainId);
			if (native)
				noterrainPenalty.at(terrainId.num) = true;
		}
	}
}

bool TurnInfo::isLayerAvailable(const EPathfindingLayer & layer) const
{
	switch(layer.toEnum())
	{
	case EPathfindingLayer::AIR:
		//airship with aviation uses both AIR and AVIATE layers
		if(target && target->inBoat() && (target->getBoat()->layer == EPathfindingLayer::AIR || target->getBoat()->layer == EPathfindingLayer::AVIATE))
			break;

		if(!hasFlyingMovement())
			return false;

		break;

	case EPathfindingLayer::WATER:
		if(target && target->inBoat() && target->getBoat()->layer == EPathfindingLayer::WATER)
			break;

		if(!hasWaterWalking())
			return false;

		break;
	}

	return true;
}

int TurnInfo::getMaxMovePoints(const EPathfindingLayer & layer) const
{
	if(layer == EPathfindingLayer::SAIL)
		return getMovePointsLimitWater();
	else if(layer == EPathfindingLayer::AVIATE)
		return getMovePointsLimitAir();
	else
		return getMovePointsLimitLand();
}
