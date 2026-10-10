/*
 * NewHorizonsLighthouse.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later.
 */
#include "StdInc.h"
#include "NewHorizonsLighthouse.h"
#include "TurnInfo.h"
#include "../CPlayerState.h"
#include "../callback/IGameInfoCallback.h"
#include "../entities/building/CBuilding.h"
#include "../entities/faction/CTownHandler.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../mapObjects/CGTownInstance.h"
#include "../mapObjects/MiscObjects.h"
#include "../mapping/TerrainTile.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <cmath>

namespace newHorizonsLighthouse
{
void validateRulesSerialization(const JsonNode & rules, bool supported)
{
	if(!rules.isStruct() || !rules.Struct().contains("lighthouseDeparture"))
		return;
	if(!supported)
		throw std::runtime_error("Captured Lighthouse departure rules require a new save format");
	const auto & feature = rules["lighthouseDeparture"];
	if(!feature.isStruct() || feature.Struct().size() != 2 || !feature["enabled"].isBool()
		|| !feature["seaMovementPercent"].isNumber() || !std::isfinite(feature["seaMovementPercent"].Float())
		|| feature["seaMovementPercent"].Float() != std::floor(feature["seaMovementPercent"].Float())
		|| feature["seaMovementPercent"].Integer() < 1 || feature["seaMovementPercent"].Integer() > 100)
		throw std::runtime_error("Invalid captured Lighthouse departure rules");
}

int departurePercent(const CGHeroInstance & hero)
{
	const auto & rules = hero.getPrimaryGrowthRules();
	validateRulesSerialization(rules, true);
	if(!hero.usesNewHorizonsMovement() || !rules.isStruct() || !rules.Struct().contains("lighthouseDeparture")
		|| !rules["lighthouseDeparture"]["enabled"].Bool())
		return 0;
	return rules["lighthouseDeparture"]["seaMovementPercent"].Integer();
}

void DepartureReceipt::validate() const
{
	if(!town.hasValue() || town.getNum() < 0 || day < 1)
		throw std::runtime_error("Invalid Castle Lighthouse departure receipt");
}

const CGTownInstance * departureTown(const CGHeroInstance & hero,
	const int3 & destination, bool requireBoat)
{
	if(!departurePercent(hero) || !hero.getOwner().isValidPlayer())
		return nullptr;
	const auto * tile = hero.cb->getTile(destination, false);
	if(!tile || !tile->isWater())
		return nullptr;
	if(requireBoat)
	{
		if(tile->visitableObjects.empty())
			return nullptr;
		const auto * boat = dynamic_cast<const CGBoat *>(hero.cb->getObjInstance(tile->visitableObjects.back()));
		if(!boat || boat->layer != EPathfindingLayer::SAIL || boat->getBoardedHero())
			return nullptr;
	}
	const auto * owner = hero.cb->getPlayerState(hero.getOwner());
	if(!owner)
		return nullptr;
	const CGTownInstance * result = nullptr;
	for(const auto * town : owner->getTowns())
	{
		if(!town || town->getOwner() != hero.getOwner() || town->getFactionID() != FactionID::CASTLE
			|| !town->hasBuilt(BuildingID::SPECIAL_1) || !town->hasBuilt(BuildingID::SHIPYARD))
			continue;
		std::vector<int3> offsets;
		town->getOutOffsets(offsets);
		if(std::ranges::none_of(offsets, [town, &destination](const int3 & offset)
			{ return town->visitablePos() + offset == destination; }))
			continue;
		if(!result || town->id < result->id)
			result = town;
	}
	return result;
}

bool hasDepartureBonus(const CGHeroInstance & hero, int turn)
{
	const auto bonuses = hero.getBonuses(Selector::typeSubtype(
		BonusType::MOVEMENT, BonusCustomSubtype::heroMovementSea));
	const CSelector day = Selector::days(turn);
	return std::ranges::any_of(*bonuses, [&day](const auto & bonus)
	{
		return bonus->stacking == STACKING_KEY && day(bonus.get());
	});
}

bool isLegacyCastleBonus(const Bonus & bonus)
{
	return bonus.type == BonusType::MOVEMENT
		&& bonus.subtype == BonusSubtypeID(BonusCustomSubtype::heroMovementSea)
		&& bonus.source == BonusSource::TOWN_STRUCTURE
		&& bonus.sid == BonusSourceID(BuildingTypeUniqueID(FactionID::CASTLE, BuildingID::SPECIAL_1))
		&& bonus.valType == BonusValueType::ADDITIVE_VALUE && bonus.val == 500;
}

Bonus departureBonus(const CGTownInstance & town, const CGHeroInstance & hero)
{
	const auto * building = town.getTown()->buildings.at(BuildingID::SPECIAL_1).get();
	Bonus bonus(BonusDuration::ONE_DAY, BonusType::MOVEMENT, BonusSource::TOWN_STRUCTURE,
		departurePercent(hero), BonusSourceID(building->getUniqueTypeID()));
	bonus.subtype = BonusCustomSubtype::heroMovementSea;
	bonus.valType = BonusValueType::PERCENT_TO_BASE;
	bonus.stacking = STACKING_KEY;
	bonus.description.appendTextID(building->getDescriptionTextID());
	return bonus;
}

int movementAfterDeparture(int remaining, int stepCost, const TurnInfo & info)
{
	const int sourceLimit = info.getMaxMovePoints(EPathfindingLayer::LAND);
	if(sourceLimit <= 0 || remaining < stepCost || stepCost < 0)
		return 0;
	const int64_t result = static_cast<int64_t>(remaining - stepCost)
		* info.getLighthouseSeaMovePoints() / sourceLimit;
	return static_cast<int>(std::clamp<int64_t>(result, 0, std::numeric_limits<int>::max()));
}
}
