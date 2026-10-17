/*
 * ElementalConsumptionValidation.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ElementalConsumptionContext.h"
#include "ElementalRebirthLineage.h"
#include "../CCreatureHandler.h"

namespace newHorizonsElementalRebirth
{
void validatePhoenixSparkConsumption(ElementalConsumptionContext & battle, const PhoenixSparkConsumption & consumption)
{
	consumption.validateShape();
	const auto profile = activeProfile(battle.getSideHero(consumption.side));
	const auto sources = battle.getUnitsIf([&](const battle::Unit * unit)
	{
		return unit && unit->unitId() == consumption.sourceUnitId;
	});
	if(!profile || !profile->phoenixSpark || battle.getPhoenixSparkUsed(consumption.side) || sources.size() != 1)
		throw std::runtime_error("Unavailable Phoenix Spark death receipt");
	const auto * source = sources.front();
	const auto category = source->unitType()
		? battle.getCreatureCategoryRules().lookup(source->unitType()->getJsonKey()) : std::nullopt;
	const DeathSnapshot snapshot{source->unitId(), consumption.side, source->getPosition(),
		source->getBattleStartMaximumAggregateHP(), *profile};
	if(source->isHypnotized() || source->isTimeStopped()
		|| !category || category->category != newHorizonsCreatures::CreatureCategory::CHAMPION
		|| !stillEligibleDeath(source, snapshot, true, false, false))
		throw std::runtime_error("Invalid Phoenix Spark original Champion death");
}

void validateChainConsumption(ElementalConsumptionContext & battle, const ChainConsumption & consumption,
	const battle::UnitInfo & spawn)
{
	consumption.validateShape();
	if(consumption.rollback)
		throw std::runtime_error("Rebirth Chain ADD cannot restore its combat token");
	const auto sources = battle.getUnitsIf([&](const battle::Unit * unit)
	{
		return unit && unit->unitId() == consumption.sourceUnitId;
	});
	const auto profile = activeProfile(battle.getSideHero(consumption.side));
	if(sources.size() != 1 || !profile || !profile->rebirthChain || battle.getRebirthChainUsed(consumption.side))
		throw std::runtime_error("Unavailable Rebirth Chain consumption");
	const auto * source = sources.front();
	const auto existingOutput = battle.getUnitsIf([&](const battle::Unit * unit)
	{
		return unit && unit->unitId() == consumption.spawnUnitId;
	});
	if(source->alive() || source->unitSide() != consumption.side || !isFirstGenerationOutput(*source)
		|| !existingOutput.empty() || !spawn.position.isValid()
		|| spawn.id != consumption.spawnUnitId || spawn.side != consumption.side
		|| spawn.position != source->getPosition() || !spawn.summoned || spawn.natureSummoned
		|| spawn.phantomIntegrity != 0 || spawn.phantomDuration != 0 || spawn.rebirthOriginalAggregateHP != 0
		|| !isCanonicalElemental(spawn.type))
		throw std::runtime_error("Invalid Rebirth Chain source or output");
	const auto category = battle.getCreatureCategoryRules().lookup(spawn.type.toCreature()->getJsonKey());
	const auto sourceCategory = battle.getCreatureCategoryRules().lookup(source->unitType()->getJsonKey());
	const auto maximumHP = battle.maximumSummonHealth(consumption.side, spawn.type);
	const DeathSnapshot snapshot{source->unitId(), consumption.side, source->getPosition(), 0,
		*profile, true, source->getRebirthOriginalAggregateHP()};
	const auto primary = battle.primaryElemental();
	if((profile->adaptiveElement || profile->perfectConvergence) && (!primary || spawn.type != *primary))
		throw std::runtime_error("Invalid terrain-selected Rebirth Chain output");
	const auto health = spawnHealth(targetHP(snapshot, spawn.type, battle), maximumHP);
	if(!category || category->category != newHorizonsCreatures::CreatureCategory::ELITE
		|| !sourceCategory || sourceCategory->category != newHorizonsCreatures::CreatureCategory::ELITE
		|| !health || spawn.count != health->count)
		throw std::runtime_error("Invalid Rebirth Chain output health or category");
}

void validateChainRollback(ElementalConsumptionContext & battle, const ChainConsumption & consumption)
{
	consumption.validateShape();
	const auto sources = battle.getUnitsIf([&](const battle::Unit * unit)
	{
		return unit && unit->unitId() == consumption.sourceUnitId;
	});
	const auto outputs = battle.getUnitsIf([&](const battle::Unit * unit)
	{
		return unit && unit->unitId() == consumption.spawnUnitId;
	});
	if(!consumption.rollback || !battle.getRebirthChainUsed(consumption.side)
		|| sources.size() != 1 || outputs.size() != 1)
		throw std::runtime_error("Unavailable Rebirth Chain rollback");
	const auto * source = sources.front();
	const auto * output = outputs.front();
	if(source->alive() || source->unitSide() != consumption.side || !isFirstGenerationOutput(*source)
		|| !output->alive() || output->unitSide() != consumption.side || !output->isSummoned() || output->isClone()
		|| output->getPhantomInitialIntegrity() != 0 || output->getRebirthOriginalAggregateHP() != 0
		|| output->getPosition() != source->getPosition() || !isCanonicalElemental(output->creatureId())
		|| output->getAvailableHealth() != static_cast<int64_t>(output->getCount()) * output->getMaxHealth())
		throw std::runtime_error("Invalid Rebirth Chain rollback source or output");
}
int64_t targetHP(const DeathSnapshot & snapshot, CreatureID creature, const ElementalConsumptionContext & battle)
{
	const auto base = targetHP(snapshot);
	if(base <= 0 || !snapshot.profile.elementalAttunement)
		return base;
	const auto primary = battle.primaryElemental();
	if(!primary || creature != *primary)
		return base;
	// floor(120% of the computed pool), without multiplying a large HP value.
	// Chain uses its captured first-output HP in targetHP above, never current wounds.
	const auto additional = base / 5;
	if(base > std::numeric_limits<int64_t>::max() - additional)
		return 0;
	return base + additional;
}
}
