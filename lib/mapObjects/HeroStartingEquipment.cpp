/*
 * HeroStartingEquipment.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "CGHeroInstance.h"
#include "../CCreatureHandler.h"
#include "../callback/IGameInfoCallback.h"
#include "../entities/artifact/CArtifact.h"
#include "../entities/creature/NewHorizonsAssimilation.h"
#include "../entities/hero/CHero.h"
#include "../entities/hero/HeroStartingProjection.h"
#include "../entities/hero/NewHorizonsHeroRules.h"
#include "../entities/hero/NewHorizonsCommandArmy.h"
#include "../gameState/CGameState.h"
#include "../IGameSettings.h"
#include "../serializer/JsonSerializeFormat.h"
#include <vstd/RNG.h>
#include <utility>

void CGHeroInstance::initializeBasicStartingEquipment(bool isFake)
{
	if(!vstd::contains(spells, SpellID::SPELLBOOK_PRESET))
	{
		// hero starts with default spellbook presence status
		if(!getArt(ArtifactPosition::SPELLBOOK) && getHeroType()->haveSpellBook	&& !isFake)
		{
			auto artifact = cb->gameState().createArtifact(ArtifactID::SPELLBOOK);
			putArtifact(ArtifactPosition::SPELLBOOK, artifact);
		}
	}
	else
		spells -= SpellID::SPELLBOOK_PRESET;

	if(!getArt(ArtifactPosition::MACH4) && !isFake)
	{
		auto artifact = cb->gameState().createArtifact(ArtifactID::CATAPULT);
		putArtifact(ArtifactPosition::MACH4, artifact); //everyone has a catapult
	}

}

bool CGHeroInstance::usesGuaranteedDefaultTent() const
{
	return newHorizonsHeroes::guaranteesDefaultFirstAidTent(getHeroTypeID(), primaryGrowthRules);
}

void CGHeroInstance::initializeDefaultStartingEquipment(bool creationInitialization)
{
	if(!creationInitialization || !usesGuaranteedDefaultTent())
		return;
	const bool useDefault = std::exchange(defaultStartingEquipmentRequested, false);
	if(!useDefault)
		return;
	const ArtifactID tent(ArtifactID::FIRST_AID_TENT);
	const auto * type = tent.toArtifact();
	const auto & slots = type->getPossibleSlots().at(ArtBearer::HERO);
	if(slots.empty() || !isPositionFree(slots.front()))
		return;
	auto * artifact = cb->gameState().createArtifact(tent);
	putArtifact(slots.front(), artifact);
}

void CGHeroInstance::serializeJsonArtifacts(JsonSerializeFormat & handler, const std::string & fieldName, CMap * map)
{
	if(!handler.saving)
		defaultStartingEquipmentRequested = !handler.getCurrent().isStruct()
			|| !handler.getCurrent().Struct().contains(fieldName);
	if(handler.saving && !defaultStartingEquipmentRequested
		&& artifactsWorn.empty() && artifactsInBackpack.empty())
	{
		// The generic artifact writer omits empty sets, which would invent DEFAULT on reload.
		auto authoredEmpty = handler.enterStruct(fieldName);
		return;
	}
	CArtifactSet::serializeJsonArtifacts(handler, fieldName, map);
}

void CGHeroInstance::initArmy(vstd::RNG & rand, IArmyDescriptor * dst)
{
	if(!dst)
		dst = this;

	int warMachinesGiven = 0;
	std::vector<newHorizonsHeroes::CommandPlacementStack> commandStacks;

	auto stacksCountChances = cb->getSettings().getVector(EGameSettings::HEROES_STARTING_STACKS_CHANCES);
	int stacksCountInitRandomNumber = rand.nextInt(1, 100);

	size_t maxStacksCount = std::min(stacksCountChances.size(), getHeroType()->initialArmy.size());

	for(int stackNo=0; stackNo < maxStacksCount; stackNo++)
	{
		if (stacksCountInitRandomNumber > stacksCountChances[stackNo])
			continue;

		auto stack = getHeroType()->initialArmy[stackNo];

		int count = rand.nextInt(stack.minAmount, stack.maxAmount);

		if(stack.creature == CreatureID::NONE)
		{
			logGlobal->error("Hero %s has invalid creature in initial army", getNameTextID());
			continue;
		}

		stack.creature = newHorizonsHeroes::defaultStartingArmyCreature(*getHeroType(),
			getCapabilityRules(), stack.creature,
			newHorizonsCreatures::isCommandSpecialistCreature(*getHeroType(), getPrimaryGrowthRules(),
				getMagicRules(), getCreatureLineSpecialtyTarget(), stack.creature));
		const CCreature * creature = stack.creature.toCreature();

		if(creature->warMachine != ArtifactID::NONE) //war machine
		{
			warMachinesGiven++;
			// These two NH default machines have explicit creation/equipment intent, not a stack roll.
			if(creature->warMachine == ArtifactID::FIRST_AID_TENT && usesGuaranteedDefaultTent())
				continue;
			if(dst != this)
				continue;

			ArtifactID aid = creature->warMachine;
			const CArtifact * art = aid.toArtifact();

			if(art != nullptr && !art->getPossibleSlots().at(ArtBearer::HERO).empty())
			{
				//TODO: should we try another possible slots?
				ArtifactPosition slot = art->getPossibleSlots().at(ArtBearer::HERO).front();

				if(!getArt(slot))
				{
					auto artifact = cb->gameState().createArtifact(aid);
					putArtifact(slot, artifact);
				}
				else
					logGlobal->warn("Hero %s already has artifact at %d, omitting giving artifact %d", getNameTextID(), slot.toEnum(), aid.toEnum());
			}
			else
			{
				logGlobal->error("Hero %s has invalid war machine in initial army", getNameTextID());
			}
		}
		else
		{
			if(getArmyCommand())
			{
				commandStacks.push_back({stack.creature, count, newHorizonsHeroes::commandArmyCreatureTraits(*this, stack.creature),
					SlotID(stackNo - warMachinesGiven)});
				continue;
			}
			if(const auto capacity = getLeadershipSlotCapacity(stack.creature))
			{
				if(count > capacity->maximum)
				{
					logGlobal->debug("Clamping starting army of hero %s from %d to %d %s for Leadership %d",
						getNameTextID(), count, capacity->maximum, creature->getJsonKey(), capacity->leadership);
					count = newHorizonsHeroes::clampStartingArmyCount(count, capacity->maximum);
				}
			}
			dst->setCreature(SlotID(stackNo-warMachinesGiven), stack.creature, count);
		}
	}
	if(getArmyCommand())
	{
		const auto plan = newHorizonsHeroes::planCommandPlacement(getArmyCommand()->effective(), commandStacks);
		if(!plan) throw std::runtime_error("Starting army has no legal Command slot arrangement: " + getHeroType()->getJsonKey());
		for(const auto & row : *plan)
			dst->setCreature(row.slot, commandStacks[row.inputIndex].creature, static_cast<TQuantity>(commandStacks[row.inputIndex].count));
	}
}
