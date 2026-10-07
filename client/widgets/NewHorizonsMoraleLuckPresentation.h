/*
 * NewHorizonsMoraleLuckPresentation.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

namespace newHorizonsMoraleLuckPresentation
{
	inline constexpr int ATTRIBUTE_LIMIT = 10;

	struct LuckReadback
	{
		int value = 0;
		bool noLuck = false;
		bool maxLuck = false;
		bool maximumLuckLimitPresent = false;
		int maximumLuckLimit = 0;

		bool hasSpecialExplanation() const
		{
			return noLuck || maxLuck || maximumLuckLimitPresent;
		}
	};

	struct MoraleReadback
	{
		int value = 0;
		bool noMorale = false;
		bool moraleImmune = false;
		bool maxMorale = false;
		bool minimumMoralePresent = false;
		int minimumMorale = 0;

		bool hasSpecialExplanation() const
		{
			return noMorale || moraleImmune || maxMorale || minimumMoralePresent;
		}
	};

	inline std::optional<LuckReadback> luckReadback(bool newHorizonsRulesActive, std::int64_t rawLuck,
		bool hasNoLuck, bool hasMaxLuck, bool hasMaximumLuckLimit, int maximumLuckLimit)
	{
		if(!newHorizonsRulesActive)
			return std::nullopt;

		LuckReadback result;
		result.maxLuck = hasMaxLuck;
		result.noLuck = hasNoLuck && !hasMaxLuck;
		result.maximumLuckLimitPresent = hasMaximumLuckLimit && (!result.noLuck || result.maxLuck);
		result.maximumLuckLimit = maximumLuckLimit;

		std::int64_t value = hasMaxLuck ? ATTRIBUTE_LIMIT
			: hasNoLuck ? 0
			: std::clamp<std::int64_t>(rawLuck, -ATTRIBUTE_LIMIT, ATTRIBUTE_LIMIT);
		if(result.maximumLuckLimitPresent)
			value = std::min<std::int64_t>(value, maximumLuckLimit);
		result.value = static_cast<int>(std::clamp<std::int64_t>(value, -ATTRIBUTE_LIMIT, ATTRIBUTE_LIMIT));
		return result;
	}

	inline std::optional<MoraleReadback> moraleReadback(bool newHorizonsRulesActive, std::int64_t rawMorale,
		bool hasNoMorale, bool hasMaxMorale, bool unaffectedByMorale,
		bool hasMinimumMorale, int minimumMorale,
		int lowerLimit = -ATTRIBUTE_LIMIT, int upperLimit = ATTRIBUTE_LIMIT)
	{
		if(!newHorizonsRulesActive)
			return std::nullopt;

		MoraleReadback result;
		result.maxMorale = hasMaxMorale;
		result.noMorale = hasNoMorale && !hasMaxMorale;
		result.moraleImmune = unaffectedByMorale && !hasNoMorale && !hasMaxMorale;
		result.minimumMoralePresent = hasMinimumMorale && !hasNoMorale && !hasMaxMorale && !unaffectedByMorale;
		result.minimumMorale = minimumMorale;

		std::int64_t value = 0;
		if(hasMaxMorale)
			value = upperLimit;
		else if(!hasNoMorale && !unaffectedByMorale)
		{
			value = std::clamp<std::int64_t>(rawMorale, lowerLimit, upperLimit);
			if(result.minimumMoralePresent)
				value = std::max<std::int64_t>(value, minimumMorale);
		}
		result.value = static_cast<int>(std::clamp<std::int64_t>(value, lowerLimit, upperLimit));
		return result;
	}
}
