/*
 * NewHorizonsNecromancy.cpp, part of VCMI / New Horizons
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "NewHorizonsNecromancy.h"

#include "../../CCreatureHandler.h"
#include "../../bonuses/BonusEnum.h"
#include "../creature/NewHorizonsCreatureCategoryRules.h"

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

int32_t countLivingEligibleCoreCasualties(const std::map<CreatureID, si32> & casualties,
	const newHorizonsCreatures::CreatureCategoryRules & categoryRules)
{
	int64_t count = 0;
	for(const auto & [creatureId, amount] : casualties)
	{
		if(amount <= 0 || !creatureId.hasValue() || !isLivingCreature(creatureId.toCreature()))
			continue;

		const auto category = newHorizonsCreatures::creatureCategoryView(categoryRules, creatureId);
		if(category && category->category == newHorizonsCreatures::CreatureCategory::CORE)
			count += amount;
	}
	return clampCount(count);
}

int32_t countLivingEligibleEliteCasualties(const std::map<CreatureID, si32> & casualties,
	const newHorizonsCreatures::CreatureCategoryRules & categoryRules)
{
	int64_t count = 0;
	for(const auto & [creatureId, amount] : casualties)
	{
		if(amount <= 0 || !creatureId.hasValue() || !isLivingCreature(creatureId.toCreature()))
			continue;

		const auto category = newHorizonsCreatures::creatureCategoryView(categoryRules, creatureId);
		if(category && category->category == newHorizonsCreatures::CreatureCategory::ELITE)
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

NecromancyResult resolve(int rank, int32_t eligibleCasualties, int32_t eligibleCoreCasualties,
	bool boneCollector, bool corpsePreservation, bool darkConversionAvailable,
	bool skeletonSlotAvailable, bool zombieSlotAvailable, int32_t currentMana, int32_t manaLimit,
	int32_t eligibleEliteCasualties, bool soulHarvester, bool wightSlotAvailable)
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

	// Preserve the single global Necromancy floor for base Skeletons. Separately
	// floor each captured category's casualty share before checking its conversion
	// groups. Inputs are clamped to a disjoint partition of the total so malformed
	// or saturated counts cannot consume more Skeletons than the global result.
	const auto coreCasualties = std::clamp(eligibleCoreCasualties, 0, result.eligibleCasualties);
	const auto eliteCasualties = std::clamp(eligibleEliteCasualties, 0,
		result.eligibleCasualties - coreCasualties);
	const auto coreSkeletons = clampCount(static_cast<int64_t>(coreCasualties) * result.percentage / 100);
	const auto eliteSkeletons = clampCount(static_cast<int64_t>(eliteCasualties) * result.percentage / 100);
	if(darkConversionAvailable)
		result.zombiesRaised = coreSkeletons / 3;
	if(soulHarvester)
		result.wightsRaised = eliteSkeletons / 6;
	const int64_t convertedSkeletons = static_cast<int64_t>(result.zombiesRaised) * 3
		+ static_cast<int64_t>(result.wightsRaised) * 6;
	result.skeletonsRaised = clampCount(static_cast<int64_t>(result.skeletonsOffered) - convertedSkeletons);
	result.darkConversionChosen = result.zombiesRaised > 0;

	// Preflight every output stack before callers mutate the army.
	const bool skeletonsFit = result.skeletonsRaised == 0 || skeletonSlotAvailable;
	const bool zombiesFit = result.zombiesRaised == 0 || zombieSlotAvailable;
	const bool wightsFit = result.wightsRaised == 0 || wightSlotAvailable;
	if(!skeletonsFit || !zombiesFit || !wightsFit)
	{
		result.skeletonsRaised = 0;
		result.zombiesRaised = 0;
		result.wightsRaised = 0;
		result.darkConversionChosen = false;
		result.blockedByArmyCapacity = true;
		return result;
	}

	result.applied = true;
	const int outputKinds = (result.skeletonsRaised > 0) + (result.zombiesRaised > 0) + (result.wightsRaised > 0);
	if(outputKinds == 1 && result.zombiesRaised > 0)
		result.raisedCreature = CreatureID(CreatureID::decode("core:zombie"));
	else if(outputKinds == 1 && result.wightsRaised > 0)
		result.raisedCreature = CreatureID(CreatureID::decode("core:wight"));
	else if(outputKinds == 1 && result.skeletonsRaised > 0)
		result.raisedCreature = CreatureID(CreatureID::decode("core:skeleton"));

	const int32_t totalRaised = result.skeletonsRaised + result.zombiesRaised + result.wightsRaised;
	if(totalRaised >= 10)
	{
		const int32_t available = std::max(0, manaLimit - currentMana);
		result.manaRecovered = std::min(10, totalRaised / 10);
		result.manaRecovered = std::min(result.manaRecovered, available);
	}
	return result;
}
}
