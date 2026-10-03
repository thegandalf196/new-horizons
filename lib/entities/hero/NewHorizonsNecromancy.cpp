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

bool isEligibleNonlivingCreature(const CCreature * creature)
{
	return creature
		&& creature->hasBonusOfType(BonusType::NON_LIVING)
		&& !creature->hasBonusOfType(BonusType::UNDEAD)
		&& !creature->hasBonusOfType(BonusType::MECHANICAL);
}

bool isEligibleUndeadCreature(const CCreature * creature)
{
	return creature
		&& creature->hasBonusOfType(BonusType::UNDEAD)
		&& !creature->hasBonusOfType(BonusType::MECHANICAL);
}

int32_t clampCount(int64_t value)
{
	return static_cast<int32_t>(std::clamp<int64_t>(value, 0, std::numeric_limits<int32_t>::max()));
}

template<typename EligibilityPredicate>
SpecialCasualtyCounts countSpecialCasualties(const std::map<CreatureID, si32> & casualties,
	const newHorizonsCreatures::CreatureCategoryRules & categoryRules, EligibilityPredicate eligibleCreature)
{
	int64_t total = 0;
	int64_t core = 0;
	int64_t elite = 0;
	for(const auto & [creatureId, amount] : casualties)
	{
		if(amount <= 0 || !creatureId.hasValue() || !eligibleCreature(creatureId.toCreature()))
			continue;

		total += amount;
		const auto category = newHorizonsCreatures::creatureCategoryView(categoryRules, creatureId);
		if(category && category->category == newHorizonsCreatures::CreatureCategory::CORE)
			core += amount;
		else if(category && category->category == newHorizonsCreatures::CreatureCategory::ELITE)
			elite += amount;
	}

	SpecialCasualtyCounts result;
	result.total = clampCount(total);
	result.core = std::min(clampCount(core), result.total);
	result.elite = std::min(clampCount(elite), result.total - result.core);
	return result;
}

SpecialCasualtyCounts normalizeSpecialCounts(SpecialCasualtyCounts counts)
{
	counts.total = std::max(0, counts.total);
	counts.core = std::clamp(counts.core, 0, counts.total);
	counts.elite = std::clamp(counts.elite, 0, counts.total - counts.core);
	return counts;
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

SpecialCasualtyCounts countEligibleNonlivingCasualties(const std::map<CreatureID, si32> & casualties,
	const newHorizonsCreatures::CreatureCategoryRules & categoryRules)
{
	return countSpecialCasualties(casualties, categoryRules, isEligibleNonlivingCreature);
}

SpecialCasualtyCounts countEligibleUndeadCasualties(const std::map<CreatureID, si32> & casualties,
	const newHorizonsCreatures::CreatureCategoryRules & categoryRules)
{
	return countSpecialCasualties(casualties, categoryRules, isEligibleUndeadCreature);
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
	int32_t eligibleEliteCasualties, bool soulHarvester, bool wightSlotAvailable, CreatureID skeletonOutput,
	SpecialCasualtyCounts nonliving, SpecialCasualtyCounts undead,
	bool lordOfTheDead, bool defeatedArmyHadLivingChampion, bool boneDragonSlotAvailable)
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
	nonliving = normalizeSpecialCounts(nonliving);
	undead = normalizeSpecialCounts(undead);
	result.deathLordCasualties = nonliving.total;
	result.graveKnowledgeCasualties = undead.total;
	result.deathLordSkeletons = clampCount(static_cast<int64_t>(nonliving.total) * result.percentage / 400);
	result.graveKnowledgeSkeletons = clampCount(static_cast<int64_t>(undead.total) * 20 / 100);
	const auto ordinarySkeletons = clampCount(static_cast<int64_t>(result.eligibleCasualties) * result.percentage / 100);
	result.skeletonsOffered = clampCount(static_cast<int64_t>(ordinarySkeletons)
		+ result.deathLordSkeletons + result.graveKnowledgeSkeletons);
	if(result.skeletonsOffered <= 0)
		return result;

	// Preserve the single global Necromancy floor for ordinary living Skeletons.
	// Special casualty pools use their own floored rates, and category-specific
	// contributions from each pool are summed only after those independent floors.
	// Inputs are clamped to disjoint category partitions so malformed or saturated
	// counts cannot consume more Skeletons than the generated pools provide.
	const auto coreCasualties = std::clamp(eligibleCoreCasualties, 0, result.eligibleCasualties);
	const auto eliteCasualties = std::clamp(eligibleEliteCasualties, 0,
		result.eligibleCasualties - coreCasualties);
	int64_t coreSkeletons = static_cast<int64_t>(coreCasualties) * result.percentage / 100
		+ static_cast<int64_t>(nonliving.core) * result.percentage / 400
		+ static_cast<int64_t>(undead.core) * 20 / 100;
	int64_t eliteSkeletons = static_cast<int64_t>(eliteCasualties) * result.percentage / 100
		+ static_cast<int64_t>(nonliving.elite) * result.percentage / 400
		+ static_cast<int64_t>(undead.elite) * 20 / 100;
	// Keep all conversion inputs disjoint and bounded by the gross generated
	// Skeleton pool even when independently saturated casualty counts disagree.
	coreSkeletons = std::clamp<int64_t>(coreSkeletons, 0, result.skeletonsOffered);
	eliteSkeletons = std::clamp<int64_t>(eliteSkeletons, 0,
		static_cast<int64_t>(result.skeletonsOffered) - coreSkeletons);
	int64_t unclassifiedSkeletons = static_cast<int64_t>(result.skeletonsOffered)
		- coreSkeletons - eliteSkeletons;
	if(lordOfTheDead && defeatedArmyHadLivingChampion && result.skeletonsOffered >= 12)
	{
		int64_t remainingCost = 12;
		const int64_t fromUnclassified = std::min(unclassifiedSkeletons, remainingCost);
		remainingCost -= fromUnclassified;
		const int64_t fromElite = std::min(eliteSkeletons, remainingCost);
		eliteSkeletons -= fromElite;
		remainingCost -= fromElite;
		const int64_t fromCore = std::min(coreSkeletons, remainingCost);
		coreSkeletons -= fromCore;
		remainingCost -= fromCore;
		if(remainingCost == 0)
		{
			result.lordOfDeadSkeletonsConsumed = 12;
			result.boneDragonsRaised = 1;
		}
	}
	if(darkConversionAvailable)
		result.zombiesRaised = clampCount(coreSkeletons / 3);
	if(soulHarvester)
		result.wightsRaised = clampCount(eliteSkeletons / 6);
	const int64_t convertedSkeletons = static_cast<int64_t>(result.boneDragonsRaised) * 12
		+ static_cast<int64_t>(result.zombiesRaised) * 3
		+ static_cast<int64_t>(result.wightsRaised) * 6;
	result.skeletonsRaised = clampCount(static_cast<int64_t>(result.skeletonsOffered) - convertedSkeletons);
	result.darkConversionChosen = result.zombiesRaised > 0;

	// Preflight every output stack before callers mutate the army.
	const bool skeletonsFit = result.skeletonsRaised == 0 || skeletonSlotAvailable;
	const bool zombiesFit = result.zombiesRaised == 0 || zombieSlotAvailable;
	const bool wightsFit = result.wightsRaised == 0 || wightSlotAvailable;
	const bool boneDragonsFit = result.boneDragonsRaised == 0 || boneDragonSlotAvailable;
	if(!skeletonsFit || !zombiesFit || !wightsFit || !boneDragonsFit)
	{
		result.skeletonsRaised = 0;
		result.zombiesRaised = 0;
		result.wightsRaised = 0;
		result.lordOfDeadSkeletonsConsumed = 0;
		result.boneDragonsRaised = 0;
		result.skeletonCreature = CreatureID::NONE;
		result.darkConversionChosen = false;
		result.blockedByArmyCapacity = true;
		return result;
	}

	result.applied = true;
	const auto baseSkeleton = CreatureID(CreatureID::decode("core:skeleton"));
	if(result.skeletonsRaised > 0 && skeletonOutput.hasValue() && skeletonOutput != baseSkeleton)
		result.skeletonCreature = skeletonOutput;

	const int outputKinds = (result.skeletonsRaised > 0) + (result.zombiesRaised > 0)
		+ (result.wightsRaised > 0) + (result.boneDragonsRaised > 0);
	if(outputKinds == 1 && result.zombiesRaised > 0)
		result.raisedCreature = CreatureID(CreatureID::decode("core:zombie"));
	else if(outputKinds == 1 && result.wightsRaised > 0)
		result.raisedCreature = CreatureID(CreatureID::decode("core:wight"));
	else if(outputKinds == 1 && result.boneDragonsRaised > 0)
		result.raisedCreature = CreatureID(CreatureID::decode("core:boneDragon"));
	else if(outputKinds == 1 && result.skeletonsRaised > 0)
		result.raisedCreature = result.skeletonCreature.hasValue() ? result.skeletonCreature : baseSkeleton;

	const int32_t totalRaised = result.skeletonsRaised + result.zombiesRaised
		+ result.wightsRaised + result.boneDragonsRaised;
	if(totalRaised >= 10)
	{
		const int32_t available = std::max(0, manaLimit - currentMana);
		result.manaRecovered = std::min(10, totalRaised / 10);
		result.manaRecovered = std::min(result.manaRecovered, available);
	}
	return result;
}
}
