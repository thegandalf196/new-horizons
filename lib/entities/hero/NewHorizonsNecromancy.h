/*
 * NewHorizonsNecromancy.h, part of VCMI / New Horizons
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "../../constants/EntityIdentifiers.h"

#include <cstdint>
#include <map>
#include <stdexcept>
#include <vector>

class CCreature;

namespace newHorizonsCreatures
{
class CreatureCategoryRules;
}

namespace newHorizonsNecromancy
{
inline constexpr const char * SKILL_ID = "new-horizons:necromancy";
inline constexpr const char * BONE_COLLECTOR_ID = "new-horizons:necromancy.boneCollector";
inline constexpr const char * CORPSE_PRESERVATION_ID = "new-horizons:necromancy.corpsePreservation";
inline constexpr const char * DARK_CONVERSION_ID = "new-horizons:necromancy.darkConversion";
inline constexpr const char * BLACK_HARVEST_ID = "new-horizons:necromancy.blackHarvest";
inline constexpr const char * SOUL_HARVESTER_ID = "new-horizons:necromancy.soulHarvester";
inline constexpr const char * MASTER_OF_BONES_ID = "new-horizons:necromancy.masterOfBones";

/// The post-battle payload is deliberately explicit.  The client must be able
/// to explain what the authoritative server actually raised, including a
/// legal zero result caused by army capacity.
struct DLL_LINKAGE NecromancyResult
{
	bool active = false;
	bool boneCollector = false;
	bool corpsePreservation = false;
	bool darkConversionAvailable = false;
	/// Historical field name retained in the versioned result payload. This is
	/// true only when the automatic Core-only conversion actually produced Zombies.
	bool darkConversionChosen = false;
	bool applied = false;
	bool blockedByArmyCapacity = false;

	int32_t rank = 0;
	int32_t percentage = 0;
	int32_t eligibleCasualties = 0;
	int32_t skeletonsOffered = 0;
	int32_t skeletonsRaised = 0;
	int32_t zombiesRaised = 0;
	int32_t wightsRaised = 0;
	/// NONE means the ordinary Skeleton form; set only for an applied upgraded output.
	CreatureID skeletonCreature = CreatureID::NONE;
	int32_t manaRecovered = 0;

	CreatureID raisedCreature = CreatureID::NONE;

	template <typename Handler>
	void serialize(Handler & h)
	{
		if(h.saving && !isSkeletonOutputValid())
			throw std::runtime_error("Invalid Necromancy Skeleton output form");
		if(h.saving && skeletonCreature != CreatureID::NONE
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_NECROMANCY_SKELETON_FORM))
			throw std::runtime_error("Cannot write Necromancy Skeleton form to an older format");
		if(h.saving && wightsRaised < 0)
			throw std::runtime_error("Invalid negative Necromancy Wight count");
		if(h.saving && wightsRaised != 0
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_NECROMANCY_WIGHTS))
			throw std::runtime_error("Cannot write Necromancy Wights to an older format");
		h & active;
		h & boneCollector;
		h & corpsePreservation;
		h & darkConversionAvailable;
		h & darkConversionChosen;
		h & applied;
		h & blockedByArmyCapacity;
		h & rank;
		h & percentage;
		h & eligibleCasualties;
		h & skeletonsOffered;
		h & skeletonsRaised;
		h & zombiesRaised;
		h & manaRecovered;
		h & raisedCreature;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_NECROMANCY_WIGHTS))
		{
			h & wightsRaised;
		}
		else if(!h.saving)
		{
			wightsRaised = 0;
		}
		if(wightsRaised < 0)
			throw std::runtime_error("Invalid negative Necromancy Wight count");
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_NECROMANCY_SKELETON_FORM))
		{
			h & skeletonCreature;
		}
		else if(!h.saving)
		{
			skeletonCreature = CreatureID::NONE;
		}
		if(!isSkeletonOutputValid())
			throw std::runtime_error("Invalid Necromancy Skeleton output form");
	}

	bool isSkeletonOutputValid() const
	{
		return skeletonCreature == CreatureID::NONE || (skeletonCreature.hasValue() && skeletonsRaised > 0);
	}

	bool empty() const
	{
		return !active && !boneCollector && !corpsePreservation && !darkConversionAvailable
			&& !darkConversionChosen && !applied && !blockedByArmyCapacity
			&& rank == 0 && percentage == 0 && eligibleCasualties == 0
			&& skeletonsOffered == 0 && skeletonsRaised == 0 && zombiesRaised == 0
			&& wightsRaised == 0 && manaRecovered == 0 && raisedCreature == CreatureID::NONE
			&& skeletonCreature == CreatureID::NONE;
	}
};

struct DLL_LINKAGE DestinationPlan
{
	SlotID skeleton;
	SlotID zombie;
	bool fits = false;
};

/// Reserve distinct army destinations for every non-empty conversion output.
/// Existing matching stacks take precedence and do not consume a free slot.
DLL_LINKAGE DestinationPlan reserveDestinations(SlotID existingSkeleton, SlotID existingZombie,
	std::vector<SlotID> freeSlots, int32_t skeletonCount, int32_t zombieCount);

/// Count casualties which leave an ordinary, raisable corpse.  The battle
/// result supplies provenance-filtered casualties where available; this helper
/// is also the compatibility fallback for a result created by an older wire
/// format.
DLL_LINKAGE int32_t countLivingEligibleCasualties(const std::map<CreatureID, si32> & casualties);

/// Count only explicitly captured Core-category casualties that leave an
/// ordinary raisable corpse. Missing creature-category context never infers a tier.
DLL_LINKAGE int32_t countLivingEligibleCoreCasualties(const std::map<CreatureID, si32> & casualties,
	const newHorizonsCreatures::CreatureCategoryRules & categoryRules);

/// Count only explicitly captured Elite-category casualties that leave an
/// ordinary raisable corpse. Missing creature-category context never infers a tier.
DLL_LINKAGE int32_t countLivingEligibleEliteCasualties(const std::map<CreatureID, si32> & casualties,
	const newHorizonsCreatures::CreatureCategoryRules & categoryRules);

/// Resolve base Necromancy once over all eligible casualties; Dark Conversion
/// consumes complete groups of three Skeletons attributable to Core casualties,
/// while Soul Harvester consumes complete groups of six attributable to Elite
/// casualties. Each category is independently floored for its conversion gate,
/// while every remainder from global base rounding remains Skeletons. Slot
/// booleans preflight all output stacks atomically before state mutation.
DLL_LINKAGE NecromancyResult resolve(int rank, int32_t eligibleCasualties, int32_t eligibleCoreCasualties,
	bool boneCollector, bool corpsePreservation, bool darkConversionAvailable,
	bool skeletonSlotAvailable, bool zombieSlotAvailable, int32_t currentMana, int32_t manaLimit,
	int32_t eligibleEliteCasualties = 0, bool soulHarvester = false, bool wightSlotAvailable = true,
	CreatureID skeletonOutput = CreatureID::NONE);
}
