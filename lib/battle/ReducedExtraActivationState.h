/*
 * ReducedExtraActivationState.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>

#include "../serializer/ESerializationVersion.h"

/// Side-owned once-per-combat allowance and current reduced-output activation.
struct DLL_LINKAGE ReducedExtraActivationState
{
	static constexpr uint32_t INVALID_UNIT_ID = std::numeric_limits<uint32_t>::max();

	bool enabled = false;
	bool used = false;
	uint32_t activeUnitId = INVALID_UNIT_ID;
	int32_t outputPercent = 100;

	bool hasActiveUnit() const
	{
		return activeUnitId != INVALID_UNIT_ID;
	}

	bool operator==(const ReducedExtraActivationState &) const = default;

	void validateShape() const
	{
		if(!enabled)
		{
			if(*this != ReducedExtraActivationState{})
				throw std::runtime_error("Disabled reduced extra activation has saved state");
			return;
		}

		if(!used && (hasActiveUnit() || outputPercent != 100))
			throw std::runtime_error("Unused reduced extra activation carries an active unit");
		if(used && hasActiveUnit() && (outputPercent < 1 || outputPercent >= 100))
			throw std::runtime_error("Active reduced extra activation has an invalid output percentage");
		if(used && !hasActiveUnit() && outputPercent != 100)
			throw std::runtime_error("Inactive reduced extra activation carries an output percentage");
	}

	void validateTransitionFrom(const ReducedExtraActivationState & previous) const
	{
		previous.validateShape();
		validateShape();
		if(*this == previous)
			return;
		if(!previous.enabled || !enabled)
			throw std::runtime_error("Reduced extra activation update enables or resets its allowance");

		const bool spendsAllowance = !previous.used && used && hasActiveUnit()
			&& !previous.hasActiveUnit() && previous.outputPercent == 100;
		const bool clearsActivation = previous.used && previous.hasActiveUnit() && used
			&& !hasActiveUnit() && outputPercent == 100;
		if(!spendsAllowance && !clearsActivation)
			throw std::runtime_error("Invalid reduced extra activation state transition");
	}

	template <typename Handler>
	void serialize(Handler & h)
	{
		const auto feature = Handler::Version::NEW_HORIZONS_REDUCED_EXTRA_ACTIVATION;
		if(h.saving && !h.hasFeature(feature) && *this != ReducedExtraActivationState{})
			throw std::runtime_error("Cannot discard reduced extra activation state");

		if(h.hasFeature(feature))
		{
			h & enabled;
			h & used;
			h & activeUnitId;
			h & outputPercent;
		}
		else if(!h.saving)
		{
			*this = {};
		}

		if(!h.saving)
			validateShape();
	}
};
