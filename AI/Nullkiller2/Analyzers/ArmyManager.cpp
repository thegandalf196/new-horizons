/*
* BuildingManager.cpp, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/

#include "StdInc.h"
#include "ArmyManager.h"
#include "../Helpers/ArmyFormation.h"
#include "../Helpers/LeadershipEvaluation.h"
#include "../Engine/Nullkiller.h"
#include "../../../lib/mapObjects/MapObjects.h"
#include "../../../lib/mapping/TerrainTile.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/TerrainHandler.h"
#include "../../../lib/battle/NewHorizonsDiscipline.h"
#include "../../../lib/entities/creature/NewHorizonsRecruitmentTraining.h"
#include "../../../lib/entities/hero/NewHorizonsLeadership.h"
#include <limits>

namespace NK2AI
{
StackUpgradeInfo::StackUpgradeInfo(CreatureID initial, CreatureID upgraded, int count)
	:initialCreature(initial), upgradedCreature(upgraded), count(count)
{
	cost = (upgradedCreature.toCreature()->getFullRecruitCost() - initialCreature.toCreature()->getFullRecruitCost()) * count;
	const auto delta = std::max<int64_t>(0, int64_t(upgradedCreature.toCreature()->getAIValue())
		- initialCreature.toCreature()->getAIValue());
	upgradeValue = static_cast<uint64_t>(delta) * std::max(0, count);
}

void ArmyUpgradeInfo::addArmyToBuy(std::vector<SlotInfo> army)
{
	for(auto slot : army)
	{
		resultingArmy.push_back(slot);

		upgradeValue = static_cast<int64_t>(std::min<uint64_t>(std::numeric_limits<int64_t>::max(),
			leadershipAI::addPower(static_cast<uint64_t>(std::max<int64_t>(0, upgradeValue)), slot.power)));
		upgradeCost += slot.creature->getFullRecruitCost() * slot.count;
	}
}

void ArmyUpgradeInfo::addArmyToGet(std::vector<SlotInfo> army)
{
	for(auto slot : army)
	{
		resultingArmy.push_back(slot);

		upgradeValue = static_cast<int64_t>(std::min<uint64_t>(std::numeric_limits<int64_t>::max(),
			leadershipAI::addPower(static_cast<uint64_t>(std::max<int64_t>(0, upgradeValue)), slot.power)));
	}
}

std::vector<SlotInfo> ArmyManager::toSlotInfo(std::vector<creInfo> army) const
{
	std::vector<SlotInfo> result;

	for(auto i : army)
	{
		SlotInfo slot;

		slot.creature = i.creID.toCreature();
		slot.count = i.count;
		slot.power = evaluateStackPower(i.creID.toCreature(), i.count);

		result.push_back(slot);
	}

	return result;
}

uint64_t ArmyManager::howManyReinforcementsCanGet(const CGHeroInstance * hero, const CCreatureSet * source) const
{
	return howManyReinforcementsCanGet(hero, hero, source, aiNk->cc->getTile(hero->visitablePos())->getTerrainID());
}

std::vector<SlotInfo>::iterator ArmyManager::getBestUnitForScout(std::vector<SlotInfo> & army, const TerrainId & armyTerrain) const
{
	uint64_t totalPower = 0;

	for (const auto & unit : army)
		totalPower = leadershipAI::addPower(totalPower, unit.power);

	int baseMovementCost = cpsic->getSettings().getInteger(EGameSettings::HEROES_MOVEMENT_COST_BASE);
	bool terrainHasPenalty = armyTerrain.hasValue() && armyTerrain.toEntity(LIBRARY)->moveCost != baseMovementCost;

	// arbitrary threshold - don't give scout more than specified part of total AI value of our army
	uint64_t maxUnitValue = totalPower / 100;

	const auto & movementPointsLimits = cpsic->getSettings().getVector(EGameSettings::HEROES_MOVEMENT_POINTS_LAND);

	auto fastest = std::ranges::min_element(army, [&](const SlotInfo & left, const SlotInfo & right) -> bool
	{
		uint64_t leftUnitPower = left.power / left.count;
		uint64_t rightUnitPower = right.power / right.count;
		bool leftUnitIsWeak = leftUnitPower < maxUnitValue || left.creature->getLevel() < 4;
		bool rightUnitIsWeak = rightUnitPower < maxUnitValue || right.creature->getLevel() < 4;

		if (leftUnitIsWeak != rightUnitIsWeak)
			return leftUnitIsWeak;

		if (terrainHasPenalty)
		{
			const auto leftFaction = left.creature->getFactionID().toFaction();
			const auto rightFaction = right.creature->getFactionID().toFaction();
			const bool leftIsNative = leftFaction->isNativeTerrain(armyTerrain);
			const bool rightIsNative = rightFaction->isNativeTerrain(armyTerrain);

			if (leftIsNative != rightIsNative)
			{
				if (leftIsNative)
					return true;

				if (rightIsNative)
					return false;
			}
		}

		int leftEffectiveMovement = std::min<int>(movementPointsLimits.size() - 1, left.creature->getMovementRange());
		int rightEffectiveMovement = std::min<int>(movementPointsLimits.size() - 1, right.creature->getMovementRange());

		int leftMovementPointsLimit = movementPointsLimits[leftEffectiveMovement];
		int rightMovementPointsLimit = movementPointsLimits[rightEffectiveMovement];

		if (leftMovementPointsLimit != rightMovementPointsLimit)
			return leftMovementPointsLimit > rightMovementPointsLimit;

		return leftUnitPower < rightUnitPower;
	});

	return fastest;
}

class TemporaryArmy : public CArmedInstance
{
	const CGHeroInstance * commander;
public:
	const CGHeroInstance * moraleCommander() const override { return commander; }
	void armyChanged() override {}
	explicit TemporaryArmy(const CGHeroInstance * hero = nullptr)
		:CArmedInstance(nullptr, BonusNodeType::UNKNOWN, true), commander(hero)
	{
	}
};

std::vector<SlotInfo> ArmyManager::getBestArmy(const IBonusBearer * armyCarrier, const CCreatureSet * target,
	const CCreatureSet * source, const TerrainId & armyTerrain, const CGHeroInstance * sourceCarrier,
	armyFormation::ArmyExchangeProjection * exchangePlan) const
{
	if(exchangePlan)
		*exchangePlan = {};
	const auto * receiverHero = dynamic_cast<const CGHeroInstance *>(armyCarrier);
	auto sortedSlots = getLeadershipSortedSlots(target, source, receiverHero);
	if(source->stacksCount() == 0)
	{
		if(armyFormation::hasLeadershipCapacityRules(receiverHero, target))
		{
			std::vector<SlotInfo> currentArmy;
			currentArmy.reserve(target->stacksCount());
			for(const auto & slot : target->Slots())
			{
				const auto * creature = slot.second->getCreatureID().toCreature();
				currentArmy.push_back({creature, slot.second->getCount(), evaluateStackPower(creature, slot.second->getCount())});
			}
			return currentArmy;
		}
		return sortedSlots;
	}

	std::map<FactionID, uint64_t> alignmentMap;

	for(auto & slot : sortedSlots)
	{
		auto & power = alignmentMap[slot.creature->getFactionID()];
		power = leadershipAI::addPower(power, slot.power);
	}

	std::set<FactionID> allowedFactions;
	std::vector<SlotInfo> resultingArmy;
	uint64_t armyValue = 0;
	TemporaryArmy newArmyInstance(receiverHero);

	while(allowedFactions.size() < alignmentMap.size())
	{
		auto strongestAlignment = vstd::maxElementByFun(alignmentMap, [&](std::pair<FactionID, uint64_t> pair) -> uint64_t
		{
			return vstd::contains(allowedFactions, pair.first) ? 0 : pair.second;
		});

		allowedFactions.insert(strongestAlignment->first);

		std::vector<SlotInfo> newArmy;
		newArmyInstance.clearSlots();

		for(auto & slot : sortedSlots)
		{
			if(vstd::contains(allowedFactions, slot.creature->getFactionID()))
			{
				auto slotID = newArmyInstance.getFreeSlot();

				if(slotID.validSlot())
				{
					newArmyInstance.setCreature(slotID, slot.creature->getId(), slot.count);
					auto receipt = newArmyInstance.getStackPtr(slotID)->getTrainingReceipt();
					for(const auto * army : {target, source})
						for(const auto & [oldSlot, original] : army->Slots())
							if(original->getCreatureID() == slot.creature->getId())
							{
								newHorizonsTraining::Receipt origins;
								origins.mercenaryOrigins = original->getTrainingReceipt().mercenaryOrigins;
								receipt.merge(origins);
							}
					newArmyInstance.getStackPtr(slotID)->setTrainingReceipt(receipt);
					newArmy.push_back(slot);
				}
			}
		}

		newArmyInstance.updateMoraleBonusFromArmy();

		const auto newValue = evaluateCompositionArmy(receiverHero, newArmyInstance, newArmy);

		if(armyValue >= newValue)
		{
			break;
		}

		resultingArmy = newArmy;
		armyValue = newValue;
	}

	const auto * sourceHero = sourceCarrier ? sourceCarrier : dynamic_cast<const CGHeroInstance *>(source);
	const bool leadershipRulesApply = armyFormation::hasLeadershipCapacityRules(receiverHero, target)
		|| armyFormation::hasLeadershipCapacityRules(receiverHero, source)
		|| armyFormation::hasLeadershipCapacityRules(sourceHero, target)
		|| armyFormation::hasLeadershipCapacityRules(sourceHero, source);
	if(leadershipRulesApply)
		return projectArmyAdmission(target, source, receiverHero, sourceHero, resultingArmy, exchangePlan);

	if(resultingArmy.size() <= GameConstants::ARMY_SIZE
		&& allowedFactions.size() == alignmentMap.size()
		&& (sourceHero ? sourceHero->needsLastStack() : source->needsLastStack()))
	{
		auto weakest = getBestUnitForScout(resultingArmy, armyTerrain);
		if(weakest->count == 1)
		{
			if (resultingArmy.size() == 1)
				logAi->warn("ArmyManager::getBestArmy Unexpected resulting army size!");

			resultingArmy.erase(weakest);
		}
		else
		{
			weakest->power -= weakest->power / weakest->count;
			weakest->count--;
		}
	}

	return resultingArmy;
}

ui64 ArmyManager::howManyReinforcementsCanBuy(const CCreatureSet * h, const CGDwelling * t) const
{
	return howManyReinforcementsCanBuy(h, t, aiNk->getFreeResources());
}

std::shared_ptr<CCreatureSet> ArmyManager::getArmyAvailableToBuyAsCCreatureSet(
	const CGDwelling * dwelling,
	TResources availableRes,
	const CGHeroInstance * buyer) const
{
	std::vector<creInfo> creaturesInDwellings;
	auto army = std::make_shared<TemporaryArmy>();

	for(int i = dwelling->creatures.size() - 1; i >= 0; i--)
	{
		auto ci = infoFromDC(dwelling->creatures[i]);

		if(!ci.count || ci.creID == CreatureID::NONE)
			continue;

		ci.count = dwelling->getRecruitmentAmount(ci.creID, availableRes, ci.count, buyer);

		if(!ci.count)
			continue;

		SlotID dst = army->getFreeSlot();

		if(!dst.validSlot())
			break;

		army->setCreature(dst, ci.creID, ci.count);
		availableRes -= dwelling->getTotalRecruitmentCost(ci.creID, ci.count, buyer);
	}

	return army;
}

ui64 ArmyManager::howManyReinforcementsCanBuy(
	const CCreatureSet * targetArmy,
	const CGDwelling * dwelling,
	const TResources & availableResources,
	uint8_t turn) const
{
	ui64 aivalue = 0;
	auto army = getArmyAvailableToBuy(targetArmy, dwelling, availableResources);
	const auto * hero = dynamic_cast<const CGHeroInstance *>(targetArmy);
	if(hero && newHorizonsHeroes::usesLeadershipEfficiency(hero->getCapabilityRules()))
	{
		TemporaryArmy after(hero);
		for(const auto & [slot, stack] : targetArmy->Slots())
			after.setCreature(slot, stack->getCreatureID(), stack->getCount());
		for(const auto & incoming : army)
		{
			const auto slot = after.getSlotFor(incoming.creID);
			if(!slot.validSlot()) continue;
			const int present = after.hasStackAtSlot(slot) ? after.getStackCount(slot) : 0;
			const int count = std::min(incoming.count, std::numeric_limits<TQuantity>::max() - present);
			if(count > 0) after.addToSlot(slot, incoming.creID, count);
		}
		const auto beforePower = leadershipAI::armyStrength(hero, targetArmy);
		const auto afterPower = leadershipAI::armyStrength(hero, &after);
		return afterPower > beforePower ? afterPower - beforePower : 0;
	}

	for(const creInfo & ci : army)
	{
		aivalue = leadershipAI::addPower(aivalue, evaluateStackPower(ci.creID.toCreature(), ci.count));
	}

	return aivalue;
}

ui64 ArmyManager::howManyReinforcementsCanGet(const IBonusBearer * armyCarrier, const CCreatureSet * target,
	const CCreatureSet * source, const TerrainId & armyTerrain, const CGHeroInstance * sourceCarrier) const
{
	if(source->stacksCount() == 0)
	{
		return 0;
	}

	auto bestArmy = getBestArmy(armyCarrier, target, source, armyTerrain, sourceCarrier);
	uint64_t newArmy = 0;
	uint64_t oldArmy = leadershipAI::armyStrength(dynamic_cast<const CGHeroInstance *>(armyCarrier), target);

	for(auto & slot : bestArmy)
	{
		newArmy = leadershipAI::addPower(newArmy, slot.power);
	}

	return newArmy > oldArmy ? newArmy - oldArmy : 0;
}

uint64_t ArmyManager::evaluateStackPower(const Creature * creature, int count) const
{
	if(!creature || count <= 0) return 0;
	// Both source operands are int32; widen before multiplying, not afterwards.
	return static_cast<uint64_t>(std::max<int64_t>(0, creature->getAIValue())) * static_cast<uint64_t>(count);
}

SlotInfo ArmyManager::getTotalCreaturesAvailable(CreatureID creatureID) const
{
	auto creatureInfo = totalArmy.find(creatureID);

	return creatureInfo == totalArmy.end() ? SlotInfo() : creatureInfo->second;
}

void ArmyManager::update()
{
	logAi->trace("Start analysing army");

	std::vector<const CCreatureSet *> total;
	auto heroes = cpsic->getHeroesInfo();
	auto towns = cpsic->getTownsInfo();

	std::copy(heroes.begin(), heroes.end(), std::back_inserter(total));
	std::copy(towns.begin(), towns.end(), std::back_inserter(total));

	totalArmy.clear();

	for(auto army : total)
	{
		for(const auto & slot : army->Slots())
		{
			accumulateArmySupply(totalArmy[slot.second->getCreatureID()],
				slot.second->getCreature(), slot.second->getCount());
		}
	}

}

std::vector<SlotInfo> ArmyManager::convertToSlots(const CCreatureSet * army) const
{
	std::vector<SlotInfo> result;

	for(const auto & slot : army->Slots())
	{
		SlotInfo slotInfo;

		slotInfo.creature = slot.second->getCreatureID().toCreature();
		slotInfo.count = slot.second->getCount();
		slotInfo.power = evaluateStackPower(slotInfo.creature, slotInfo.count);

		result.push_back(slotInfo);
	}

	return result;
}

std::vector<StackUpgradeInfo> ArmyManager::getPossibleUpgrades(
	const CCreatureSet * army, const CGObjectInstance * upgrader, const CGHeroInstance * carrier) const
{
	std::vector<StackUpgradeInfo> upgrades;

	if(upgrader->ID == Obj::HILL_FORT)
	{
		upgrades = getHillFortUpgrades(army, carrier);
	}
	else
	{
		auto dwelling = dynamic_cast<const CGDwelling *>(upgrader);

		if(dwelling)
		{
			upgrades = getDwellingUpgrades(army, dwelling, carrier);
		}
	}

	return upgrades;
}

ArmyUpgradeInfo ArmyManager::calculateCreaturesUpgrade(
	const CCreatureSet * army,
	const CGObjectInstance * upgrader,
	const TResources & availableResources,
	const CGHeroInstance * carrier) const
{
	if(!upgrader)
		return ArmyUpgradeInfo();
	if(!carrier)
		carrier = dynamic_cast<const CGHeroInstance *>(army);

	std::vector<StackUpgradeInfo> upgrades = getPossibleUpgrades(army, upgrader, carrier);

	vstd::erase_if(upgrades, [&](const StackUpgradeInfo & u) -> bool
	{
		return !availableResources.canAfford(u.cost);
	});

	if(upgrades.empty())
		return ArmyUpgradeInfo();

	std::sort(upgrades.begin(), upgrades.end(), [](const StackUpgradeInfo & u1, const StackUpgradeInfo & u2) -> bool
	{
		return u1.upgradeValue > u2.upgradeValue;
	});

	TResources resourcesLeft = availableResources;
	ArmyUpgradeInfo result;
	
	result.resultingArmy = convertToSlots(army);

	for(auto upgrade : upgrades)
	{
		if(resourcesLeft.canAfford(upgrade.cost))
		{
			SlotInfo upgradedArmy;

			upgradedArmy.creature = upgrade.upgradedCreature.toCreature();
			upgradedArmy.count = upgrade.count;
			upgradedArmy.power = evaluateStackPower(upgradedArmy.creature, upgradedArmy.count);

			auto slotToReplace = std::find_if(result.resultingArmy.begin(), result.resultingArmy.end(), [&](const SlotInfo & slot) -> bool {
				return slot.count == upgradedArmy.count && slot.creature->getId() == upgrade.initialCreature;
			});

			resourcesLeft -= upgrade.cost;
			result.upgradeCost += upgrade.cost;
			result.upgradeValue = static_cast<int64_t>(std::min<uint64_t>(std::numeric_limits<int64_t>::max(),
				leadershipAI::addPower(static_cast<uint64_t>(std::max<int64_t>(0, result.upgradeValue)), upgrade.upgradeValue)));

			*slotToReplace = upgradedArmy;
		}
	}
	if(carrier && newHorizonsHeroes::usesLeadershipEfficiency(carrier->getCapabilityRules()))
	{
		const auto before = leadershipAI::armyStrength(carrier, army);
		const auto after = leadershipArmyValue(result.resultingArmy, carrier);
		result.upgradeValue = static_cast<int64_t>(std::min<uint64_t>(std::numeric_limits<int64_t>::max(),
			after > before ? after - before : 0));
	}

	return result;
}

}
