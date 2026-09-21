/*
 * NewHorizonsPrimaryGrowth.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsPrimaryGrowth.h"

namespace newHorizonsHeroes
{
std::array<int, GameConstants::PRIMARY_SKILLS> calculatePrimaryGrowth(
	const PrimaryProfile & profile,
	std::span<const ExtraPrimaryRoll> opportunities,
	std::span<const int> draws)
{
	profile.baseAtLevel(1); // Validate the supplied profile before calculating.
	// Keep the old pure-function signature so source and saved-state consumers
	// remain compatible, but New Horizons no longer evaluates chance rows.
	// Every level grants exactly the authored class vector.
	(void)opportunities;
	(void)draws;
	return profile.growth;
}
}
