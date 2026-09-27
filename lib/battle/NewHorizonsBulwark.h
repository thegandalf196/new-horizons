/*
 * NewHorizonsBulwark.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include <cstdint>
#include <string_view>

class CGHeroInstance;

namespace newHorizonsBulwark
{
constexpr std::string_view SKILL_ID = "new-horizons:bulwarkOfTheMire";
constexpr std::string_view MIREBORN_ID = "new-horizons:bulwarkOfTheMire.mireborn";
constexpr std::string_view THICK_HIDE_ID = "new-horizons:bulwarkOfTheMire.thickHide";
constexpr std::string_view BOG_AMBUSH_ID = "new-horizons:bulwarkOfTheMire.bogAmbush";

/// Percentages are represented in basis points so Advanced's 7.5% base and
/// 0.15% per Defense remain exact throughout the server damage pipeline.
constexpr int BASIS_POINTS_PER_PERCENT = 100;

DLL_LINKAGE int rank(const CGHeroInstance * hero);
DLL_LINKAGE bool hasMireborn(const CGHeroInstance * hero);
DLL_LINKAGE bool hasThickHide(const CGHeroInstance * hero);
DLL_LINKAGE bool hasBogAmbush(const CGHeroInstance * hero);
DLL_LINKAGE int reductionBasisPoints(int rank, int heroDefense, bool mireTerrain);
DLL_LINKAGE int preemptivePercent(int rank);
DLL_LINKAGE int preemptivePercent(int rank, bool bogAmbush);
DLL_LINKAGE int reflectionPercent(int rank);
DLL_LINKAGE int reflectionBasisPoints(int rank, bool ranged, bool thickHide);
DLL_LINKAGE int64_t reflectedDamage(int64_t actualHealthLoss, int reflectionBasisPoints);
}
