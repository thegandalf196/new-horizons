/*
 * NewHorizonsPrimaryGrowth.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "NewHorizonsPrimaryProfile.h"
#include "../../constants/EntityIdentifiers.h"
#include <span>

namespace newHorizonsHeroes
{
/// One independent skill-based primary-stat bonus opportunity.
struct DLL_LINKAGE ExtraPrimaryRoll
{
	PrimarySkill attribute;
	int chancePercent = 0;
};

/// Returns the class vector plus independent version-3 bonuses. Each draw is
/// in [0, 99] and succeeds when below its percentage. Older profiles ignore
/// opportunities, preserving their fixed-vector interpretation.
DLL_LINKAGE std::array<int, GameConstants::PRIMARY_SKILLS> calculatePrimaryGrowth(
	const PrimaryProfile & profile,
	std::span<const ExtraPrimaryRoll> opportunities,
	std::span<const int> draws);
}
