/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "BattleSide.h"
#include <cstdint>
#include <limits>
#include <stdexcept>
class JsonNode;
namespace battle
{
/// One optional attack within the unit's existing Creature Activation.
/// No global activation serial: a Crisis interruption may activate another unit.
struct DLL_LINKAGE DreadCavalryState
{
	static constexpr uint32_t NO_VICTIM = std::numeric_limits<uint32_t>::max();
	bool usedThisActivation = false;
	bool pending = false;
	uint32_t victimId = NO_VICTIM;
	BattleSide controller = BattleSide::NONE;
	int32_t remainingMovement = 0;
	int32_t damagePercent = 100;
	int32_t sharedTailCap = 0;
	int32_t independentVanish = 0;
	bool operator==(const DreadCavalryState &) const = default;
	bool hasState() const;
	void validateShape() const;
	void arm(uint32_t victim, BattleSide currentController, int remaining,
		int outputPercent, int sharedCap, int vanish);
	/// Used remains true through decline, death and native Rebirth.
	void retirePending();
	int legacyTailAfterMovement(int actualCost) const;
	JsonNode toJson() const;
	static DreadCavalryState fromJson(const JsonNode & node);
	template<typename Handler> void validateSerialization(Handler & h) const
	{
		validateShape();
		if(hasState() && !h.hasFeature(Handler::Version::NEW_HORIZONS_DREAD_CAVALRY))
			throw std::runtime_error("Cannot discard Dread Cavalry activation state");
	}
	template<typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validateSerialization(h);
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_DREAD_CAVALRY))
		{
			h & usedThisActivation; h & pending; h & victimId; h & controller;
			h & remainingMovement; h & damagePercent; h & sharedTailCap; h & independentVanish;
		}
		else if(!h.saving)
			*this = {};
		if(!h.saving)
			validateShape();
	}
};
DLL_LINKAGE DreadCavalryState dreadCavalryStateFromUnitJson(const JsonNode & unit);
}
