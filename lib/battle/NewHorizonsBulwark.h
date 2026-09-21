/*
 * NewHorizonsBulwark.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include <string_view>

class CGHeroInstance;

namespace newHorizonsBulwark
{
constexpr std::string_view SKILL_ID = "new-horizons:bulwarkOfTheMire";
constexpr std::string_view MIREBORN_ID = "new-horizons:bulwarkOfTheMire.mireborn";

/// Percentages are represented in basis points so Advanced's 7.5% base and
/// 0.15% per Defense remain exact throughout the server damage pipeline.
constexpr int BASIS_POINTS_PER_PERCENT = 100;

DLL_LINKAGE int rank(const CGHeroInstance * hero);
DLL_LINKAGE bool hasMireborn(const CGHeroInstance * hero);
DLL_LINKAGE int reductionBasisPoints(int rank, int heroDefense, bool mireTerrain);
DLL_LINKAGE int preemptivePercent(int rank);
DLL_LINKAGE int reflectionPercent(int rank);
}
