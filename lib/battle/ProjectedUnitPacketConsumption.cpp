/*
 * ProjectedUnitPacketConsumption.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedBattleWorldArenaState.h"
#include "ElementalConsumptionContext.h"
#include "../networkPacks/PacksForClientBattle.h"

bool battle::ProjectedBattleWorldArena::Data::applyConsumption(ProjectedBattleWorldArena & owner,
	const BattleUnitsChanged & packet)
{
	class Context final : public newHorizonsElementalRebirth::ElementalConsumptionContext
	{
		Data & data;
	public:
		explicit Context(Data & data) : data(data) {}
		Units getUnitsIf(const UnitFilter & predicate) const override
		{
			Units result;
			for(const auto * unit : data.roster.orderedUnits())
				if(predicate(unit))
					result.push_back(unit);
			return result;
		}
		const CGHeroInstance * getSideHero(BattleSide side) const override { return data.sideHeroes.at(side); }
		bool getPhoenixSparkUsed(BattleSide side) const override { return data.sparkUsed.at(side); }
		bool getRebirthChainUsed(BattleSide side) const override { return data.chainUsed.at(side); }
		const newHorizonsCreatures::CreatureCategoryRules & getCreatureCategoryRules() const override { return data.categories; }
		std::optional<CreatureID> primaryElemental() const override { return data.primaryElemental; }
		int32_t maximumSummonHealth(BattleSide side, CreatureID type) override
		{
			UnitInfo info;
			info.type = type;
			info.side = side;
			return data.summonCapacity(info);
		}
	} context(*this);
	if(packet.phoenixSparkConsumption)
	{
		newHorizonsElementalRebirth::validatePhoenixSparkConsumption(context, *packet.phoenixSparkConsumption);
		sparkUsed.at(packet.phoenixSparkConsumption->side) = true;
		return true;
	}
	if(!packet.rebirthChainConsumption)
		return false;
	const auto & consumption = *packet.rebirthChainConsumption;
	if(consumption.rollback)
	{
		newHorizonsElementalRebirth::validateChainRollback(context, consumption);
		owner.removeUnit(consumption.spawnUnitId);
		chainUsed.at(consumption.side) = false;
		return true;
	}
	UnitInfo output;
	output.load(packet.changedStacks.front().id, packet.changedStacks.front().data);
	newHorizonsElementalRebirth::validateChainConsumption(context, consumption, output);
	chainUsed.at(consumption.side) = true;
	try
	{
		owner.addUnit(output.id, packet.changedStacks.front().data);
	}
	catch(...)
	{
		chainUsed.at(consumption.side) = false;
		throw;
	}
	return true;
}
