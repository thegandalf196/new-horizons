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

#include <array>
#include <cstdint>
#include <limits>
#include <optional>

namespace
{
struct NewHorizonsMovementModifiers
{
	int64_t base = 0;
	int64_t percentageToBase = 0;
	int64_t percentageToAll = 0;
	int64_t additive = 0;
	// BonusList::totalValue has a distinct fallback when the list contains
	// independent bounds only.  Source/target percentage entries count as
	// non-independent entries there, even though they do not add a value by
	// themselves.
	int nonIndependentBonusCount = 0;
	// BonusList calls these independentMin (at most) and independentMax (at
	// least).  Keep the original names so their clamping semantics remain
	// explicit when the final movement value is produced.
	std::optional<int64_t> independentMin;
	std::optional<int64_t> independentMax;
};

int64_t saturatingAdd(const int64_t lhs, const int64_t rhs)
{
	if(rhs > 0 && lhs > std::numeric_limits<int64_t>::max() - rhs)
		return std::numeric_limits<int64_t>::max();
	if(rhs < 0 && lhs < std::numeric_limits<int64_t>::min() - rhs)
		return std::numeric_limits<int64_t>::min();
	return lhs + rhs;
}

int64_t saturatingMultiply(const int64_t lhs, const int64_t rhs)
{
	if(lhs == 0 || rhs == 0)
		return 0;
	if(lhs == -1 && rhs == std::numeric_limits<int64_t>::min())
		return std::numeric_limits<int64_t>::max();
	if(rhs == -1 && lhs == std::numeric_limits<int64_t>::min())
		return std::numeric_limits<int64_t>::max();

	if(lhs > 0)
	{
		if(rhs > 0 && lhs > std::numeric_limits<int64_t>::max() / rhs)
			return std::numeric_limits<int64_t>::max();
		if(rhs < 0 && rhs < std::numeric_limits<int64_t>::min() / lhs)
			return std::numeric_limits<int64_t>::min();
	}
	else
	{
		if(rhs > 0 && lhs < std::numeric_limits<int64_t>::min() / rhs)
			return std::numeric_limits<int64_t>::min();
		if(rhs < 0 && lhs < std::numeric_limits<int64_t>::max() / rhs)
			return std::numeric_limits<int64_t>::max();
	}
	return lhs * rhs;
}

int64_t applyPercentageRoundDown(const int64_t base, const int64_t percentage)
{
	const int64_t factor = saturatingAdd(100, percentage);
	return saturatingMultiply(base, factor) / 100;
}

int64_t applyPercentageRoundUp(const int64_t base, const int64_t percentage)
{
	const int64_t factor = saturatingAdd(100, percentage);
	const int64_t product = saturatingMultiply(base, factor);
	// This mirrors BonusList::totalValue: creature-ability values are the
	// sole existing bonus family that rounds up instead of down.
	return (base >= 0 ? saturatingAdd(product, 99) : saturatingAdd(product, -99)) / 100;
}

NewHorizonsMovementModifiers getNewHorizonsMovementModifiers(
	const TConstBonusListPtr & bonuses, const CSelector & daySelector)
{
	NewHorizonsMovementModifiers result;
	std::array<int64_t, vstd::to_underlying(BonusSource::NUM_BONUS_SOURCE)> percentToSource = {};

	// BonusList first gathers source/target-type percentage modifiers, then
	// applies them to each value before dispatching by value type.  Reproduce
	// that ordering here instead of flattening only PERCENT_TO_BASE and
	// ADDITIVE_VALUE entries; movement effects are still ordinary BonusList
	// values and must retain those semantics.
	for(const auto & bonus : *bonuses)
	{
		if(!daySelector(bonus.get()))
			continue;

		switch(bonus->valType)
		{
		case BonusValueType::PERCENT_TO_SOURCE:
			percentToSource[vstd::to_underlying(bonus->source)] = saturatingAdd(
				percentToSource[vstd::to_underlying(bonus->source)], bonus->val);
			break;
		case BonusValueType::PERCENT_TO_TARGET_TYPE:
			percentToSource[vstd::to_underlying(bonus->targetSourceType)] = saturatingAdd(
				percentToSource[vstd::to_underlying(bonus->targetSourceType)], bonus->val);
			break;
		default:
			break;
		}
	}

	for(const auto & bonus : *bonuses)
	{
		if(!daySelector(bonus.get()))
			continue;
		if(bonus->valType != BonusValueType::INDEPENDENT_MIN
			&& bonus->valType != BonusValueType::INDEPENDENT_MAX)
			++result.nonIndependentBonusCount;

		const auto sourceIndex = vstd::to_underlying(bonus->source);
		const auto sourcePercentage = percentToSource[sourceIndex];
		const int64_t modifiedValue = bonus->source == BonusSource::CREATURE_ABILITY
			? applyPercentageRoundUp(bonus->val, sourcePercentage)
			: applyPercentageRoundDown(bonus->val, sourcePercentage);

		switch(bonus->valType)
		{
		case BonusValueType::PERCENT_TO_BASE:
			result.percentageToBase = saturatingAdd(result.percentageToBase, modifiedValue);
			break;
		case BonusValueType::PERCENT_TO_ALL:
			result.percentageToAll = saturatingAdd(result.percentageToAll, modifiedValue);
			break;
		case BonusValueType::BASE_NUMBER:
			result.base = saturatingAdd(result.base, modifiedValue);
			break;
		case BonusValueType::ADDITIVE_VALUE:
			result.additive = saturatingAdd(result.additive, modifiedValue);
			break;
		case BonusValueType::INDEPENDENT_MAX:
			if(!result.independentMax || modifiedValue > *result.independentMax)
				result.independentMax = modifiedValue;
			break;
		case BonusValueType::INDEPENDENT_MIN:
			if(!result.independentMin || modifiedValue < *result.independentMin)
				result.independentMin = modifiedValue;
			break;
		default:
			// Source/target modifiers affect the values above but do not add a
			// value of their own, matching BonusList::totalValue.
			break;
		}
	}
	return result;
}

bool isCoreLogisticsMovementBonus(const Bonus & bonus)
{
	// The legacy Logistics entity predates the New Horizons split movement
	// selectors and only carries a land bonus.  New Horizons heroes use the
	// same Logistics percentage for both movement pools, while legacy heroes
	// must continue to use the original land-only bonus.
	return bonus.source == BonusSource::SECONDARY_SKILL
		&& bonus.sid == BonusSourceID(SecondarySkill(SecondarySkill::LOGISTICS));
}

TConstBonusListPtr newHorizonsWaterBonuses(
	const TConstBonusListPtr & landBonuses, const TConstBonusListPtr & waterBonuses)
{
	const bool waterHasCoreLogistics = std::ranges::any_of(*waterBonuses,
		[](const std::shared_ptr<const Bonus> & bonus)
		{
			return isCoreLogisticsMovementBonus(*bonus);
		});
	if(waterHasCoreLogistics)
		return waterBonuses;

	TBonusListPtr merged = std::make_shared<BonusList>();
	for(const auto & bonus : *waterBonuses)
		merged->push_back(bonus);

	for(const auto & bonus : *landBonuses)
	{
		if(!isCoreLogisticsMovementBonus(*bonus))
			continue;

		auto seaBonus = std::make_shared<Bonus>(*bonus);
		seaBonus->subtype = BonusSubtypeID(BonusCustomSubtype::heroMovementSea);
		merged->push_back(std::move(seaBonus));
	}

	return merged;
}

int applyNewHorizonsMovementBounds(const int movement, const NewHorizonsMovementModifiers & modifiers)
{
	if(modifiers.nonIndependentBonusCount == 0)
	{
		// This is BonusList::totalValue's independent-only fallback.  A sole
		// INDEPENDENT_MIN is an upper-bound value but is returned directly, and
		// likewise a sole INDEPENDENT_MAX is returned directly as the lower-bound
		// value; do not clamp either one against the canonical 200-point base.
		if(modifiers.independentMin && !modifiers.independentMax)
			return static_cast<int>(std::clamp<int64_t>(*modifiers.independentMin, 0, std::numeric_limits<int>::max()));
		if(modifiers.independentMax && !modifiers.independentMin)
			return static_cast<int>(std::clamp<int64_t>(*modifiers.independentMax, 0, std::numeric_limits<int>::max()));
		if(modifiers.independentMin && modifiers.independentMax)
		{
			int64_t lower = *modifiers.independentMax;
			const int64_t upper = *modifiers.independentMin;
			if(upper < lower)
				lower = upper;
			const int64_t fallback = std::clamp<int64_t>(0, lower, upper);
			return static_cast<int>(std::clamp<int64_t>(fallback, 0, std::numeric_limits<int>::max()));
		}
	}

	int64_t lower = modifiers.independentMax.value_or(std::numeric_limits<int64_t>::min());
	int64_t upper = modifiers.independentMin.value_or(std::numeric_limits<int64_t>::max());
	if(modifiers.independentMin && modifiers.independentMax && upper < lower)
		lower = upper;

	const int64_t bounded = std::clamp<int64_t>(movement, lower, upper);
	return static_cast<int>(std::clamp<int64_t>(bounded, 0, std::numeric_limits<int>::max()));
}
}

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

TurnInfo::TurnInfo(TurnInfoCache * sharedCache, const CGHeroInstance * target, int Turn,
	const CCreatureSet * projectedArmy)
	: target(target)
	, noterrainPenalty(LIBRARY->terrainTypeHandler->size())
{
	CSelector daySelector = Selector::days(Turn);
	const bool newHorizonsMovement = target->usesNewHorizonsMovement();

	int lowestSpeed = 10;
	if(!newHorizonsMovement)
	{
		if (target->getTreeVersion() == sharedCache->heroLowestSpeedVersion)
		{
			lowestSpeed = sharedCache->heroLowestSpeedValue;
		}
		else
		{
			lowestSpeed = target->getLowestCreatureSpeed();
			sharedCache->heroLowestSpeedValue = lowestSpeed;
			sharedCache->heroLowestSpeedVersion = target->getTreeVersion();
		}
	}

	{
		static const CSelector selector = Selector::type()(BonusType::WATER_WALKING);
		const auto & bonuses = sharedCache->waterWalking.getBonusList(target, selector);
		waterWalkingTest = bonuses->getFirst(daySelector) != nullptr;
		waterWalkingValue = bonuses->valOfBonuses(daySelector);
	}

	{
		static const CSelector selector = Selector::type()(BonusType::FLYING_MOVEMENT);
		const auto & bonuses = sharedCache->flyingMovement.getBonusList(target, selector);
		flyingMovementTest = bonuses->getFirst(daySelector) != nullptr;
		flyingMovementValue = bonuses->valOfBonuses(daySelector);
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

	if(newHorizonsMovement)
	{
		static const CSelector landSelector = Selector::typeSubtype(BonusType::MOVEMENT, BonusCustomSubtype::heroMovementLand);
		static const CSelector waterSelector = Selector::typeSubtype(BonusType::MOVEMENT, BonusCustomSubtype::heroMovementSea);
		const auto landBonuses = sharedCache->movementPointsLimitLand.getBonusList(target, landSelector);
		const auto waterBonuses = newHorizonsWaterBonuses(landBonuses,
			sharedCache->movementPointsLimitWater.getBonusList(target, waterSelector));
		const auto landModifiers = getNewHorizonsMovementModifiers(landBonuses, daySelector);
		const auto waterModifiers = getNewHorizonsMovementModifiers(waterBonuses, daySelector);
		movePointsLimitLand = applyNewHorizonsMovementBounds(
			newHorizonsMovement::maximumDailyMovement(
				saturatingAdd(newHorizonsMovement::BASE_DAILY_MOVEMENT, landModifiers.base),
				landModifiers.percentageToBase, landModifiers.percentageToAll, landModifiers.additive),
			landModifiers);
		movePointsLimitWater = applyNewHorizonsMovementBounds(
			newHorizonsMovement::maximumDailyMovement(
				saturatingAdd(newHorizonsMovement::BASE_DAILY_MOVEMENT, waterModifiers.base),
				waterModifiers.percentageToBase, waterModifiers.percentageToAll, waterModifiers.additive),
			waterModifiers);
	}
	else
	{
		static const CSelector selector = Selector::typeSubtype(BonusType::MOVEMENT, BonusCustomSubtype::heroMovementSea);
		const auto & vectorSea = target->cb->getSettings().getValue(EGameSettings::HEROES_MOVEMENT_POINTS_SEA).Vector();
		const auto & bonuses = sharedCache->movementPointsLimitWater.getBonusList(target, selector);
		int baseMovementPointsSea;
		if (lowestSpeed < vectorSea.size())
			baseMovementPointsSea = vectorSea[lowestSpeed].Integer();
		else
			baseMovementPointsSea = vectorSea.back().Integer();

		movePointsLimitWater = bonuses->valOfBonuses(daySelector, baseMovementPointsSea);
	}

	if(!newHorizonsMovement)
	{
		static const CSelector selector = Selector::typeSubtype(BonusType::MOVEMENT, BonusCustomSubtype::heroMovementLand);
		const auto & vectorLand = target->cb->getSettings().getValue(EGameSettings::HEROES_MOVEMENT_POINTS_LAND).Vector();
		const auto & bonuses = sharedCache->movementPointsLimitLand.getBonusList(target, selector);
		int baseMovementPointsLand;
		if (lowestSpeed < vectorLand.size())
			baseMovementPointsLand = vectorLand[lowestSpeed].Integer();
		else
			baseMovementPointsLand = vectorLand.back().Integer();

		movePointsLimitLand = bonuses->valOfBonuses(daySelector, baseMovementPointsLand);
	}

	{
		// A hero in an airship has 2000 movements. No modificators increasing the speed of moving either by land or water influence this quantity.
		movePointsLimitAir = 2000;
	}

	{
		static const CSelector selector = Selector::type()(BonusType::NO_TERRAIN_PENALTY);
		const auto & bonuses = sharedCache->noTerrainPenalty.getBonusList(target, selector);
		for (const auto & bonus : *bonuses)
		{
			TerrainId affectedTerrain = bonus->subtype.as<TerrainId>();
			noterrainPenalty.at(affectedTerrain.num) = true;
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
