/*
 * BattleDeploymentState.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>

#include "BattleSide.h"
#include "../GameConstants.h"
#include "../serializer/ESerializationVersion.h"

/// Independent army deployment opportunities. The ordinary tactics side and
/// distance remain projections of the current phase, not additional allowances.
struct DLL_LINKAGE BattleDeploymentState
{
	bool independent = false;
	BattleSideArray<uint8_t> distances{};
	BattleSideArray<bool> completed{};

	BattleSide activeSide() const
	{
		if(independent)
			for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
				if(distances[side] > 0 && !completed[side])
					return side;
		return BattleSide::NONE;
	}

	void complete(BattleSide side)
	{
		validateShape();
		if(side == BattleSide::NONE || side != activeSide())
			throw std::runtime_error("Deployment completion does not match the active side");
		completed[side] = true;
	}

	void validateShape() const
	{
		if(independent && distances[BattleSide::ATTACKER] == 0
			&& distances[BattleSide::DEFENDER] == 0)
			throw std::runtime_error("Independent deployment has no resolved opportunity");
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			if(distances[side] > GameConstants::BFIELD_WIDTH - 2)
				throw std::runtime_error("Deployment area exceeds the battlefield");
			if(completed[side] && distances[side] == 0)
				throw std::runtime_error("Deployment completed without an opportunity");
			if(!independent && (distances[side] != 0 || completed[side]))
				throw std::runtime_error("Independent deployment state is disabled");
		}
		if(distances[BattleSide::ATTACKER] > 0 && !completed[BattleSide::ATTACKER]
			&& completed[BattleSide::DEFENDER])
			throw std::runtime_error("Deployment phases completed out of sequence");
	}

	bool operator==(const BattleDeploymentState &) const = default;

	/// Live updates may complete the current phase, but cannot change resolved
	/// eligibility, restart a completed phase, or skip the next entitled army.
	void validateTransitionFrom(const BattleDeploymentState & previous) const
	{
		validateShape();
		previous.validateShape();
		if(*this == previous)
			return;
		if(!previous.independent || !independent || distances != previous.distances)
			throw std::runtime_error("Deployment update changes resolved opportunities");
		auto expected = previous;
		expected.complete(previous.activeSide());
		if(*this != expected)
			throw std::runtime_error("Deployment update does not complete the current phase");
	}

	template <typename Handler>
	void serialize(Handler & h)
	{
		const auto feature = Handler::Version::BATTLE_DEPLOYMENT_PHASES;
		if(h.saving)
		{
			validateShape();
			if(!h.hasFeature(feature) && *this != BattleDeploymentState{})
				throw std::runtime_error("Cannot discard independent deployment state");
		}
		if(h.hasFeature(feature))
		{
			h & independent;
			for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			{
				h & distances[side];
				h & completed[side];
			}
		}
		else if(!h.saving)
			*this = {};
		validateShape();
	}
};
