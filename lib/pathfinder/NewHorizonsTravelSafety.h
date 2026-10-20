/*
 * NewHorizonsTravelSafety.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../int3.h"
#include "../constants/EntityIdentifiers.h"
#include <optional>
#include <vector>

class IGameInfoCallback;
class CGHeroInstance;

namespace newHorizonsTravel
{
struct Step
{
	int3 destination; // visitable coordinates, not hero anchor coordinates
	EPathfindingLayer layer = EPathfindingLayer::LAND; // requested movement layer
	int movementRemaining = 0; // expected readback, never a mutation
	bool forcedMarch = false; // expected existing lastUseDay/penaltyDay receipt
	bool operator==(const Step &) const = default;
};
struct Plan
{
	int3 source;
	int initialMovement = 0;
	int day = 0;
	std::vector<Step> steps;
	bool operator==(const Plan &) const = default;
};

/// Pure deterministic same-day escape using current captured policy/capability.
/// Empty steps means already eligible to end the day, not a LAND command.
/// nullopt means required landing is unreachable: no rescue or EndTurn permission.
/// AIR/WATER steps are transit; LAND steps perform ordinary empty-tile movement.
/// No casts, visits, battles, fog reveal assumptions, teleports or day transitions.
/// Plans are ephemeral: replay must validate day/source/MP and every live readback.
DLL_LINKAGE std::optional<Plan> planRequiredLanding(const IGameInfoCallback & gameInfo,
	const CGHeroInstance & hero);
}
