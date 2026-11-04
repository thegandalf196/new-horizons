/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PassiveDivineMandateValue.h"
#include <cmath>

namespace passiveDivineMandateAI
{
std::optional<float> expectedOneDraw(
	std::span<const newHorizonsPassiveDivineMandate::SpellChoice> pool,
	const OutcomeValue & outcomeValue, Workload * workload)
{
	Workload work;
	work.spells = pool.size();
	for(const auto & spell : pool)
		work.outcomes += spell.targets.size();
	const auto publish = [&]()
	{
		if(workload) *workload = work;
	};
	if(pool.empty())
	{
		publish();
		return 0.0f;
	}
	if(!outcomeValue)
	{
		publish();
		return std::nullopt;
	}
	double spellSum = 0;
	for(const auto & spell : pool)
	{
		if(spell.targets.empty() || (spell.massive && spell.targets.size() != 1))
		{
			publish();
			return std::nullopt;
		}
		double targetSum = 0;
		for(const auto & target : spell.targets)
		{
			++work.evaluatedOutcomes;
			const auto value = outcomeValue(spell, target);
			if(!value || !std::isfinite(*value))
			{
			publish();
			return std::nullopt;
		}
			targetSum += *value;
		}
		spellSum += targetSum / static_cast<double>(spell.targets.size());
	}
	const auto result = static_cast<float>(spellSum / static_cast<double>(pool.size()));
	publish();
	return std::isfinite(result) ? std::optional<float>(result) : std::nullopt;
}
}
