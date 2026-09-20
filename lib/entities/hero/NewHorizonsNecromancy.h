/*
 * NewHorizonsNecromancy.h, part of VCMI / New Horizons
 *
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "../../constants/EntityIdentifiers.h"

#include <cstdint>
#include <map>
#include <vector>

class CCreature;

namespace newHorizonsNecromancy
{
inline constexpr const char * SKILL_ID = "new-horizons:necromancy";
inline constexpr const char * BONE_COLLECTOR_ID = "new-horizons:necromancy.boneCollector";
inline constexpr const char * CORPSE_PRESERVATION_ID = "new-horizons:necromancy.corpsePreservation";
inline constexpr const char * DARK_CONVERSION_ID = "new-horizons:necromancy.darkConversion";
inline constexpr const char * BLACK_HARVEST_ID = "new-horizons:necromancy.blackHarvest";

/// The post-battle payload is deliberately explicit.  The client must be able
/// to explain what the authoritative server actually raised, including a
/// legal zero result caused by army capacity.
struct DLL_LINKAGE NecromancyResult
{
	bool active = false;
	bool boneCollector = false;
	bool corpsePreservation = false;
	bool darkConversionAvailable = false;
	bool darkConversionChosen = false;
	bool applied = false;
	bool blockedByArmyCapacity = false;

	int32_t rank = 0;
	int32_t percentage = 0;
	int32_t eligibleCasualties = 0;
	int32_t skeletonsOffered = 0;
	int32_t skeletonsRaised = 0;
	int32_t zombiesRaised = 0;
	int32_t manaRecovered = 0;

	CreatureID raisedCreature = CreatureID::NONE;

	template <typename Handler>
	void serialize(Handler & h)
	{
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
	}

	bool empty() const
	{
		return !active && !boneCollector && !corpsePreservation && !darkConversionAvailable
			&& !darkConversionChosen && !applied && !blockedByArmyCapacity
			&& rank == 0 && percentage == 0 && eligibleCasualties == 0
			&& skeletonsOffered == 0 && skeletonsRaised == 0 && zombiesRaised == 0
			&& manaRecovered == 0 && raisedCreature == CreatureID::NONE;
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

/// Resolve the count-based New Horizons conversion.  `zombieChoice` means that
/// every complete group of three offered Skeletons becomes one Zombie; the
/// remainder stays Skeletons.  Slot booleans are part of the resolver so a
/// caller can preflight the whole conversion atomically before mutating state.
DLL_LINKAGE NecromancyResult resolve(int rank, int32_t eligibleCasualties,
	bool boneCollector, bool corpsePreservation, bool darkConversionAvailable,
	bool zombieChoice, bool skeletonSlotAvailable, bool zombieSlotAvailable,
	int32_t currentMana, int32_t manaLimit);
}
