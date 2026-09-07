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
/// One independent extra-point opportunity, resolved from saved skill rules by
/// the authoritative caller. This primitive neither offers nor grants skills.
struct DLL_LINKAGE ExtraPrimaryRoll
{
	PrimarySkill attribute;
	int chancePercent = 0;
};

/// Base growth remains ten. Every opportunity has its OWN uniform [0,99] draw;
/// successes add points instead of replacing or competing with base growth.
/// The simulation supplies draws from its existing saved RNG. No RNG, state
/// mutation or new-game activation is hidden inside this pure calculation.
DLL_LINKAGE std::array<int, GameConstants::PRIMARY_SKILLS> calculatePrimaryGrowth(
	const PrimaryProfile & profile,
	std::span<const ExtraPrimaryRoll> opportunities,
	std::span<const int> draws);
}
