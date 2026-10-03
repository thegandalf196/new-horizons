/*
 * NewHorizonsDiplomacy.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsDiplomacy.h"

#include "NewHorizonsPerkRules.h"

#include <algorithm>
#include <limits>

namespace newHorizonsDiplomacy
{
namespace
{
bool meetsArmyValueThreshold(uint64_t heroArmyValue, uint64_t creatureArmyValue,
	int32_t thresholdPercent, bool countCreatureAsHalf)
{
	if(thresholdPercent <= 0)
		return false;

	const uint64_t denominator = 100;
	const uint64_t multiplier = static_cast<uint64_t>(thresholdPercent) * (countCreatureAsHalf ? 2 : 1);
	const uint64_t whole = heroArmyValue / denominator;
	const uint64_t remainder = heroArmyValue % denominator;
	const auto maximum = std::numeric_limits<uint64_t>::max();
	if(whole > maximum / multiplier)
		return true;

	uint64_t threshold = whole * multiplier;
	const uint64_t fractional = remainder * multiplier / denominator;
	if(threshold > maximum - fractional)
		return true;

	threshold += fractional;
	return creatureArmyValue <= threshold;
}
}

bool usesNewHorizonsRules(const JsonNode & capturedPerkRules)
{
	if(!newHorizonsHeroes::usesPerkRules(capturedPerkRules))
		return false;

	const auto & diplomacy = capturedPerkRules["skills"][SKILL_ID];
	if(!diplomacy.isStruct() || !diplomacy["ranks"].isStruct())
		return false;

	for(const auto * rank : {"basic", "advanced", "expert"})
	{
		if(diplomacy["ranks"][rank]["effect"]["status"].String() == "active")
			return true;
	}
	return false;
}

Forecast resolveForecast(const ForecastInput & input)
{
	Forecast result;
	result.usesNewHorizonsRules = input.usesNewHorizonsRules;
	result.skillRank = std::clamp(input.skillRank, 0, 3);
	result.active = result.usesNewHorizonsRules && result.skillRank > 0;
	result.eligible = result.usesNewHorizonsRules && input.encounterEligible;
	result.authoredFree = result.eligible && input.authoredFree;
	result.negotiator = result.active && input.negotiator;
	result.commonCause = result.active && input.commonCause;
	result.grandDiplomat = result.active && input.grandDiplomat;
	result.heroArmyValue = input.heroArmyValue;
	result.creatureArmyValue = input.creatureArmyValue;
	result.joiningAmount = std::max<int64_t>(0, input.joiningAmount);

	if(result.active)
	{
		result.thresholdPercent = result.skillRank * 25;
		if(result.negotiator)
			result.thresholdPercent += 15;
		if(result.grandDiplomat)
			result.thresholdPercent += 25;
		result.thresholdPercent = std::min(result.thresholdPercent, 100);
	}

	if(input.goldCostPerCreature < 0 || input.joiningAmount < 0)
	{
		result.normalGoldCostValid = false;
	}
	else if(input.goldCostPerCreature > 0 && input.joiningAmount > 0
		&& input.goldCostPerCreature > std::numeric_limits<int64_t>::max() / input.joiningAmount)
	{
		result.normalGoldCostValid = false;
	}
	else
	{
		result.normalGoldCost = input.goldCostPerCreature * input.joiningAmount;
	}

	result.normalGoldCostFitsAction = result.normalGoldCostValid
		&& result.normalGoldCost <= std::numeric_limits<int>::max();
	const bool withinThreshold = result.active && result.eligible
		&& result.normalGoldCostFitsAction
		&& meetsArmyValueThreshold(result.heroArmyValue, result.creatureArmyValue,
			result.thresholdPercent, result.commonCause);
	result.willing = result.authoredFree || withinThreshold;
	return result;
}
}
