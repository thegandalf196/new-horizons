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
/// Legacy compatibility shape for old callers. New Horizons no longer
/// evaluates primary-stat chance rows.
struct DLL_LINKAGE ExtraPrimaryRoll
{
	PrimarySkill attribute;
	int chancePercent = 0;
};

/// Returns the authored class vector. The opportunity/draw spans are retained
/// only for source compatibility and are ignored.
DLL_LINKAGE std::array<int, GameConstants::PRIMARY_SKILLS> calculatePrimaryGrowth(
	const PrimaryProfile & profile,
	std::span<const ExtraPrimaryRoll> opportunities,
	std::span<const int> draws);
}
