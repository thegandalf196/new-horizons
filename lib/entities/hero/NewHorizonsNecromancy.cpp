/*
 * NewHorizonsNecromancy.cpp, part of VCMI / New Horizons
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "NewHorizonsNecromancy.h"

#include "../../CCreatureHandler.h"
#include "../../bonuses/BonusEnum.h"

#include <algorithm>
#include <limits>

namespace newHorizonsNecromancy
{
namespace
{
bool isLivingCreature(const CCreature * creature)
{
	return creature
		&& !creature->hasBonusOfType(BonusType::UNDEAD)
		&& !creature->hasBonusOfType(BonusType::NON_LIVING)
		&& !creature->hasBonusOfType(BonusType::MECHANICAL);
}

int32_t clampCount(int64_t value)
{
	return static_cast<int32_t>(std::clamp<int64_t>(value, 0, std::numeric_limits<int32_t>::max()));
}
}

int32_t countLivingEligibleCasualties(const std::map<CreatureID, si32> & casualties)
{
	int64_t count = 0;
	for(const auto & [creatureId, amount] : casualties)
	{
		if(amount <= 0 || !creatureId.hasValue() || !isLivingCreature(creatureId.toCreature()))
			continue;
		count += amount;
	}
	return clampCount(count);
}

DestinationPlan reserveDestinations(SlotID existingSkeleton, SlotID existingZombie,
	std::vector<SlotID> freeSlots, int32_t skeletonCount, int32_t zombieCount)
{
	DestinationPlan result;
	auto reserve = [&freeSlots](SlotID existing, int32_t count)
	{
		if(count <= 0)
			return SlotID();
		if(existing.validSlot())
			return existing;
		if(freeSlots.empty())
			return SlotID();
		const auto slot = freeSlots.front();
		freeSlots.erase(freeSlots.begin());
		return slot;
	};

	result.skeleton = reserve(existingSkeleton, skeletonCount);
	result.zombie = reserve(existingZombie, zombieCount);
	result.fits = (skeletonCount <= 0 || result.skeleton.validSlot())
		&& (zombieCount <= 0 || result.zombie.validSlot());
	return result;
}

NecromancyResult resolve(int rank, int32_t eligibleCasualties,
	bool boneCollector, bool corpsePreservation, bool darkConversionAvailable,
	bool zombieChoice, bool skeletonSlotAvailable, bool zombieSlotAvailable,
	int32_t currentMana, int32_t manaLimit)
{
	NecromancyResult result;
	result.active = rank >= 1 && rank <= 3;
	if(!result.active)
		return result;

	result.rank = rank;
	result.boneCollector = boneCollector;
	result.corpsePreservation = corpsePreservation;
	result.darkConversionAvailable = darkConversionAvailable;
	result.eligibleCasualties = std::max(0, eligibleCasualties);
	result.percentage = rank * 10 + (boneCollector ? 5 : 0);
	result.skeletonsOffered = clampCount(static_cast<int64_t>(result.eligibleCasualties) * result.percentage / 100);
	if(result.skeletonsOffered <= 0)
		return result;

	result.darkConversionChosen = darkConversionAvailable && zombieChoice;
	if(result.darkConversionChosen)
	{
		result.zombiesRaised = result.skeletonsOffered / 3;
		result.skeletonsRaised = result.skeletonsOffered % 3;
	}
	else
		result.skeletonsRaised = result.skeletonsOffered;

	// Preflight every output stack before callers mutate the army.  A Dark
	// Conversion result with a remainder needs both legal destinations.
	const bool skeletonsFit = result.skeletonsRaised == 0 || skeletonSlotAvailable;
	const bool zombiesFit = result.zombiesRaised == 0 || zombieSlotAvailable;
	if(!skeletonsFit || !zombiesFit)
	{
		result.skeletonsRaised = 0;
		result.zombiesRaised = 0;
		result.blockedByArmyCapacity = true;
		return result;
	}

	result.applied = true;
	if(result.zombiesRaised > 0)
		result.raisedCreature = CreatureID(CreatureID::decode("core:zombie"));
	else if(result.skeletonsRaised > 0)
		result.raisedCreature = CreatureID(CreatureID::decode("core:skeleton"));

	const int32_t totalRaised = result.skeletonsRaised + result.zombiesRaised;
	if(totalRaised >= 10)
	{
		const int32_t available = std::max(0, manaLimit - currentMana);
		result.manaRecovered = std::min(10, totalRaised / 10);
		result.manaRecovered = std::min(result.manaRecovered, available);
	}
	return result;
}
}
