/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "../StdInc.h"
#include "BuildAnalyzer.h"
#include "ArmyManager.h"
#include "../../../lib/callback/IGameInfoCallback.h"
#include "../../../lib/entities/faction/CTown.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapObjects/NewHorizonsRecruitmentGrowth.h"

namespace NK2AI
{
namespace
{
int forecastBuildingGrowth(const CGTownInstance * town, BuildingID id, const CCreature * creature, bool isBuilt)
{
	const auto & rows = town->getTown()->creatures;
	const auto row = std::ranges::find_if(rows, [creature](const auto & members)
	{
		return vstd::contains(members, creature->getId());
	});
	const int level = row == rows.end() ? creature->getLevel() - 1
		: static_cast<int>(std::distance(rows.begin(), row));
	const int buildingGrowth = town->creatureBuildingGrowth(id, creature->getId());
	if(isBuilt && buildingGrowth <= 0)
		return town->creatureGrowth(level); // This complete total is already scaled.

	const auto percent = newHorizonsRecruitmentGrowth::percentage(town->cb->getSettings());
	if(!isBuilt && id.isDwelling())
	{
		// Retain the existing future-dwelling approximation; scale its final subtotal.
		int raw = town->creatureBaseGrowth(creature->getId());
		if(town->hasBuilt(BuildingID::CASTLE))
			raw *= 2;
		else if(town->hasBuilt(BuildingID::CITADEL))
			raw += raw / 2;
		return static_cast<int>(newHorizonsRecruitmentGrowth::scale(raw, percent));
	}

	const int contribution = buildingGrowth > 0 ? buildingGrowth : town->creatureHordeGrowth(creature->getId());
	const int current = town->getGrowthInfo(level).unscaledGrowth();
	const int before = isBuilt ? std::max(0, current - contribution) : current;
	// A contribution of one to an odd subtotal can add zero after final ceil.
	// Preserve the legacy contribution approximation, not a simulated town graph.
	return static_cast<int>(newHorizonsRecruitmentGrowth::scale(static_cast<int64_t>(before) + contribution, percent)
		- newHorizonsRecruitmentGrowth::scale(before, percent));
}
}

BuildingInfo::BuildingInfo() {}

BuildingInfo::BuildingInfo(
	const CBuilding * building,
	const CCreature * creature,
	CreatureID baseCreature,
	const CGTownInstance * town,
	const std::unique_ptr<ArmyManager> & armyManager)
{
	id = building->bid;
	buildCost = building->resources;
	buildCostWithPrerequisites = building->resources;
	dailyIncome = building->produce;
	isBuilt = town->hasBuilt(id);
	prerequisitesCount = 1;
	name = building->getNameTranslated();

	if(creature)
	{
		creatureID = creature->getId();
		baseCreatureID = baseCreature;
		creatureUnitCost = creature->getFullRecruitCost();
		creatureLevel = creature->getLevel();
		creatureGrowth = forecastBuildingGrowth(town, id, creature, isBuilt);

		armyStrength = armyManager->evaluateStackPower(creature, creatureGrowth);
		armyCost = creatureUnitCost * creatureGrowth;
	}
}

std::string BuildingInfo::toString() const
{
	return name + ", cost: " + buildCost.toString()
		+ ", creature: " + std::to_string(creatureGrowth) + " x " + std::to_string(creatureLevel)
		+ " x " + creatureUnitCost.toString()
		+ ", daily: " + dailyIncome.toString();
}

}
