/*
 * NewHorizonsConfusionResolution.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "NewHorizonsConfusionResolution.h"

#include <cmath>

namespace newHorizonsConfusion
{
std::vector<Outcome> enumerateOutcomes(const Choices & choices,
	battle::ConfusionBehavior previous, bool confounder)
{
	using Behavior = battle::ConfusionBehavior;
	std::vector<Outcome> result;
	const auto defend = [&](double probability)
	{
		result.push_back({Behavior::DEFEND, {{EActionType::DEFEND, BattleHex::INVALID, nullptr}, false}, probability});
	};
	constexpr double FAMILY_WEIGHT = 1.0 / 3.0;
	if(choices.attacks.empty())
		defend(FAMILY_WEIGHT);
	else
	{
		const double targetWeight = FAMILY_WEIGHT / choices.attacks.size();
		for(const auto & group : choices.attacks)
		{
			if(!group.attacks.empty())
			{
				for(const auto & action : group.attacks)
					result.push_back({Behavior::ATTACK, action, targetWeight / group.attacks.size()});
			}
			else if(!group.furthestAdvances.empty())
			{
				for(const auto position : group.furthestAdvances)
					result.push_back({Behavior::ATTACK, {{EActionType::WALK, position, nullptr}, false},
						targetWeight / group.furthestAdvances.size()});
			}
			else
				defend(targetWeight);
		}
	}
	defend(FAMILY_WEIGHT);
	if(choices.wanderDestinations.empty())
		defend(FAMILY_WEIGHT);
	else
	{
		for(const auto position : choices.wanderDestinations)
			result.push_back({Behavior::WANDER, {{EActionType::WALK, position, nullptr}, false},
				FAMILY_WEIGHT / choices.wanderDestinations.size()});
	}
	const bool hasAlternative = std::ranges::any_of(result, [previous](const Outcome & outcome)
	{
		return outcome.behavior != previous;
	});
	if(confounder && previous != Behavior::NONE && hasAlternative)
	{
		std::erase_if(result, [previous](const Outcome & outcome) { return outcome.behavior == previous; });
		double total = 0.0;
		for(const auto & outcome : result)
			total += outcome.probability;
		for(auto & outcome : result)
			outcome.probability /= total;
	}
	return result;
}

std::vector<Outcome> enumerateOutcomes(const CBattleInfoCallback & battle,
	const battle::Unit * unit, battle::ConfusionBehavior previous, bool confounder)
{
	return enumerateOutcomes(enumerateChoices(battle, unit), previous, confounder);
}

const Outcome & selectOutcome(const std::vector<Outcome> & outcomes, double draw)
{
	if(outcomes.empty() || !std::isfinite(draw) || draw < 0.0 || draw >= 1.0)
		throw std::invalid_argument("Invalid Confusion outcome draw");
	double cumulative = 0.0;
	for(const auto & outcome : outcomes)
	{
		cumulative += outcome.probability;
		if(draw < cumulative)
			return outcome;
	}
	return outcomes.back(); // Floating-point sum may be just below one.
}
}
