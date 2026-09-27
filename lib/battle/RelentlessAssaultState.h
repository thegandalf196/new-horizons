/*
 * RelentlessAssaultState.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

/// Hero-side streak state for the Expert Offense Relentless Assault perk.
/// The current activation's target/bonus are kept separately so multistrikes
/// use one stable value and do not advance the streak more than once.
struct DLL_LINKAGE RelentlessAssaultState
{
	static constexpr uint32_t INVALID_TARGET = std::numeric_limits<uint32_t>::max();

	uint32_t targetUnitId = INVALID_TARGET;
	uint8_t tier = 0;
	bool activationInitialized = false;
	bool activationHadEligibleAttack = false;
	uint32_t activationTargetUnitId = INVALID_TARGET;
	uint8_t activationDamagePercent = 0;

	bool operator==(const RelentlessAssaultState &) const = default;

	bool hasState() const
	{
		return *this != RelentlessAssaultState{};
	}

	void beginActivation()
	{
		if(activationInitialized && !activationHadEligibleAttack)
		{
			targetUnitId = INVALID_TARGET;
			tier = 0;
		}
		activationInitialized = true;
		activationHadEligibleAttack = false;
		activationTargetUnitId = INVALID_TARGET;
		activationDamagePercent = 0;
	}

	int damagePercentForTarget(uint32_t target) const
	{
		if(!activationInitialized || target == INVALID_TARGET)
			return 0;
		if(activationHadEligibleAttack)
			return activationTargetUnitId == target ? activationDamagePercent : 0;
		return targetUnitId == target ? std::min<uint8_t>(3, static_cast<uint8_t>(tier + 1)) * 10 : 0;
	}

	void recordAttack(uint32_t target)
	{
		if(target == INVALID_TARGET)
			return;
		if(target > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
			throw std::runtime_error("Invalid Relentless Assault target unit ID");
		if(!activationInitialized)
			beginActivation();
		if(activationHadEligibleAttack)
		{
			if(activationTargetUnitId == target)
				return; // Same activation/multistrike: keep the current tier.

			targetUnitId = target;
			tier = 0;
			activationTargetUnitId = target;
			activationDamagePercent = 0;
			return;
		}

		if(targetUnitId == target)
			tier = std::min<uint8_t>(3, static_cast<uint8_t>(tier + 1));
		else
		{
			targetUnitId = target;
			tier = 0;
		}
		activationHadEligibleAttack = true;
		activationTargetUnitId = target;
		activationDamagePercent = tier * 10;
	}

	void validateShape() const
	{
		const auto maxWireId = static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
		if((targetUnitId != INVALID_TARGET && targetUnitId > maxWireId)
			|| (activationTargetUnitId != INVALID_TARGET && activationTargetUnitId > maxWireId)
			|| tier > 3 || activationDamagePercent > 30 || activationDamagePercent % 10 != 0
			|| (!activationInitialized && (*this != RelentlessAssaultState{}))
			|| (!activationHadEligibleAttack
				&& (activationTargetUnitId != INVALID_TARGET || activationDamagePercent != 0))
			|| (activationHadEligibleAttack
				&& (activationTargetUnitId == INVALID_TARGET || activationTargetUnitId != targetUnitId
					|| activationDamagePercent != tier * 10))
			|| (targetUnitId == INVALID_TARGET && tier != 0))
			throw std::runtime_error("Invalid Relentless Assault streak state shape");
	}

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validateShape();
		h & targetUnitId;
		h & tier;
		h & activationInitialized;
		h & activationHadEligibleAttack;
		h & activationTargetUnitId;
		h & activationDamagePercent;
		if(!h.saving)
			validateShape();
	}
};
