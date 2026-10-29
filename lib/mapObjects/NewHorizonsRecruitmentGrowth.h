/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <cstdint>
class CMap;
class IGameSettings;
class JsonNode;
namespace newHorizonsRecruitmentGrowth
{
inline constexpr auto SETTING_KEY = "recruitmentGrowthPercent";
DLL_LINKAGE int percentage(const IGameSettings & settings);
/// Apply once to the final newly generated recruitment amount. Zero disables growth.
/// Quotient/remainder arithmetic preserves signed64 safety without intermediate overflow.
DLL_LINKAGE int64_t scale(int64_t count, int percent);
/// Validated representation at recruitment-stock insertion boundaries.
DLL_LINKAGE uint32_t generatedAmount(int64_t count, const IGameSettings & settings);
/// Calendar effects on stored stock are independent of scaling the new increment.
DLL_LINKAGE uint32_t stockAfterWeek(uint32_t previous, int64_t growth, int percent,
	bool doubleGrowth, bool plague);
DLL_LINKAGE void initializeWorld(CMap & map);
DLL_LINKAGE void validateOverrides(const JsonNode & settings, bool supported);
DLL_LINKAGE void validateSerialization(const IGameSettings & settings, bool supported);
}
