/*
 * ArcaneOverflowValue.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ArcaneOverflowValue.h"
#include "SearchBudget.h"
#include "../../lib/spells/NewHorizonsMetamagicOverflow.h"

#include <array>
#include <cmath>

namespace arcaneOverflowAI
{
std::optional<LocalUtility> expectedLocalValue(const newHorizonsMetamagic::OverflowPreview & preview,
	battleSearch::SearchBudget & budget, const LocalValue & evaluate)
{
	if(!evaluate || preview.primaryResistancePercent < 0 || preview.primaryResistancePercent > 100
		|| preview.secondaryResistancePercent < 0 || preview.secondaryResistancePercent > 100)
		return std::nullopt;
	// The existing three worlds certify deterministic Cascade. Independent
	// secondary resistance needs additional joint worlds; keep it unknown here
	// rather than treating the two resistance checks as one correlated roll.
	if(!preview.additionalRecipients.empty()
		&& (preview.additionalRecipients.size() != 1
			|| (preview.primaryResistancePercent < 100 && (preview.secondaryResistancePercent != 0
				|| preview.additionalRecipients.front().secondaryResistancePercent != 0))
			|| preview.additionalRecipients.front().conditionalRebirth
			|| !preview.additionalRecipients.front().additionalRecipients.empty())) return std::nullopt;

	const double primary = (100 - preview.primaryResistancePercent) / 100.0;
	const double secondary = (100 - preview.secondaryResistancePercent) / 100.0;
	const std::array<std::pair<Outcome, double>, 3> outcomes{{
		{Outcome::PrimaryResisted, 1 - primary},
		{Outcome::PrimaryOnly, primary * (1 - secondary)},
		{Outcome::BothLand, primary * secondary}}};
	uint32_t visits = 0;
	for(const auto & [outcome, probability] : outcomes)
		if(probability > 0) ++visits;
	budget.require(battleSearch::WorkKind::Node, visits);

	double immediate = 0, completion = 0;
	for(const auto & [outcome, probability] : outcomes)
	{
		if(probability == 0) continue;
		const auto value = evaluate(outcome);
		if(!value || !std::isfinite(value->immediate) || !std::isfinite(value->completion)) return std::nullopt;
		immediate += probability * value->immediate;
		completion += probability * value->completion;
	}
	const LocalUtility result{static_cast<float>(immediate), static_cast<float>(completion)};
	return std::isfinite(result.immediate) && std::isfinite(result.completion)
		? std::optional<LocalUtility>(result) : std::nullopt;
}
std::optional<LocalUtility> expectedSeededLocalValue(const newHorizonsMetamagic::OverflowPreview & plan,
	battleSearch::SearchBudget & budget, const std::function<std::optional<LocalUtility>()> & seedValue,
	const LocalValue & otherValue)
{
	if(!seedValue || !otherValue) return std::nullopt;
	return expectedLocalValue(plan, budget, [&](Outcome outcome)
	{
		return outcome == Outcome::BothLand ? seedValue() : otherValue(outcome);
	});
}

}
