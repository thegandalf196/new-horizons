/*
 * HeroActionSelection.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroActionSelection.h"
#include "PossibleSpellcast.h"
#include "BattleExchangeVariant.h"
#include "../../lib/spells/CSpell.h"

namespace heroActionSelection
{
namespace
{
Disposition admission(const PossibleSpellcast & selected, float baseline, bool mandatoryOrder)
{
	const bool followup = selected.metamagicFollowup || selected.divineMandateFollowup;
	// Keeping the allowance uses the cached no-cast baseline, not literal zero.
	// Values already contain the projected active exchange; do not add a second
	// baseline or reassociate the utility expression during selection.
	if(!mandatoryOrder && followup
		&& (selected.value < baseline || vstd::isAlmostEqual(selected.value, baseline)))
		return Disposition::RETAIN_FOLLOWUP;
	if(mandatoryOrder || followup
		|| (selected.value > baseline && !vstd::isAlmostEqual(selected.value, baseline)))
		return Disposition::PERFORM;
	return Disposition::USELESS;
}
}

float noCastBaseline(float cachedScore)
{
	return cachedScore > static_cast<float>(EvaluationResult::INEFFECTIVE_SCORE / 2)
		? cachedScore : 0.0f;
}

std::optional<Selection> choose(std::span<const PossibleSpellcast> candidates,
	float baseline, bool mandatoryOrder, Observer * observer)
{
	if(candidates.empty())
		return std::nullopt;
	size_t best = 0;
	for(size_t index = 0; index < candidates.size(); ++index)
	{
		const auto & candidate = candidates[index];
		if(observer)
			observer->candidate({index, candidate.command,
				candidate.spell ? candidate.spell->getId() : SpellID::NONE,
				candidate.spellOvercharge, candidate.value});
		if(index != 0 && candidates[best].value < candidate.value)
			best = index;
	}
	Selection result{best, admission(candidates[best], baseline, mandatoryOrder)};
	if(observer)
		observer->selected({result.ordinal, baseline, result.disposition});
	return result;
}
}
