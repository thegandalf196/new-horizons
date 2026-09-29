/*
 * NewHorizonsShadowGift.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <cstdint>
#include <string_view>

namespace newHorizonsShadowGift
{
inline constexpr std::string_view SPELL_ID = "new-horizons:shadowGift";
constexpr int32_t BASIS_POINTS_PER_WHOLE = 10'000;
constexpr int32_t DARK_GIFT_COST_PERCENT = 75;

DLL_LINKAGE bool isValidSacrificePercent(int32_t choicePercent);
/// Actual percentage of current and maximum HP paid by the selected tier.
/// Dark Gift reduces this cost only; it does not change the offensive tier.
DLL_LINKAGE int32_t getSacrificeCostBasisPoints(int32_t choicePercent, bool darkGift);
/// Offensive damage increase in basis points (1750 means +17.5%). The fixed
/// 1.25 term is not school-scaled; only 0.005 * Spell Power is.
DLL_LINKAGE int32_t getDamageBonusBasisPoints(int32_t choicePercent, int32_t spellPower,
	int32_t schoolCoefficientBasisPoints, int32_t warcastingBonusPercent = 0,
	int32_t empowerBonusPercent = 0);
/// Floors one selected percentage of an aggregate HP value. The engine uses
/// whole HP points for both current damage and battle-long cap loss.
DLL_LINKAGE int64_t getSacrificeHealthAmount(int64_t aggregateHealth, int32_t costBasisPoints);
}
