/*
 * LifeDrainCandidateValueReuse.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "LifeDrainCandidateValueReuse.h"
#include "HeroCandidateValueContext.h"
#include "BattleExchangeVariant.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/battle/Unit.h"
#include <cmath>
void LifeDrainCandidateValueReuse::beginCandidate(const PossibleSpellcast & candidate)
{
	if(candidate.command != HeroCommand::NONE || !candidate.spell
		|| candidate.spell->getJsonKey() != newHorizonsLifeDrain::SPELL_KEY)
		clear();
}

bool LifeDrainCandidateValueReuse::supported(const PossibleSpellcast & candidate,
	const heroActionProjection::Admission & admission, const std::vector<uint32_t> & sharedPurposeRecipients,
	const std::optional<newHorizonsLifeDrain::ProjectionReceipt> & receipt) const
{
	return receipt && admission.admitted && admission.spellAllowance && !admission.counterspellNegated
		&& admission.counterspell.resolutionKnown && admission.counterspell.negated == false
		&& sharedPurposeRecipients.empty() && candidate.command == HeroCommand::NONE && candidate.spell
		&& candidate.spell->getJsonKey() == newHorizonsLifeDrain::SPELL_KEY
		&& candidate.dest.size() == 2 && candidate.dest[0].unitValue && candidate.dest[1].unitValue
		&& admission.spellAllowance->spell == candidate.spell->getId()
		&& candidate.dest[0].unitValue->unitId() != candidate.dest[1].unitValue->unitId()
		&& candidate.dest[0].unitValue->unitId() == receipt->primaryTarget
		&& candidate.dest[0].hexValue == receipt->primaryPosition;
}

std::optional<float> LifeDrainCandidateValueReuse::find(const PossibleSpellcast & candidate,
	const heroActionProjection::Admission & admission, const std::vector<uint32_t> & sharedPurposeRecipients,
	const std::optional<newHorizonsLifeDrain::ProjectionReceipt> & receipt)
{
	if(!supported(candidate, admission, sharedPurposeRecipients, receipt))
	{
		clear();
		return std::nullopt;
	}
	if(control)
		++control->completedReceipts;
	if(control && !control->enabled)
		return std::nullopt;
	if(entry && entry->allowance == *admission.spellAllowance
		&& heroCandidateValueContext::sameCounterspell(entry->counterspell, admission.counterspell)
		&& entry->receipt == *receipt && heroCandidateValueContext::sameOptions(entry->candidate, candidate))
	{
		if(control)
			++control->hits;
		return entry->candidate.value;
	}
	return std::nullopt;
}

void LifeDrainCandidateValueReuse::remember(const PossibleSpellcast & candidate,
	const heroActionProjection::Admission & admission, const std::vector<uint32_t> & sharedPurposeRecipients,
	const std::optional<newHorizonsLifeDrain::ProjectionReceipt> & receipt)
{
	if((control && !control->enabled) || !supported(candidate, admission, sharedPurposeRecipients, receipt)
		|| !std::isfinite(candidate.value) || candidate.value == std::numeric_limits<float>::lowest()
		|| candidate.value == static_cast<float>(EvaluationResult::INEFFECTIVE_SCORE))
	{
		clear();
		return;
	}
	entry = Entry{candidate, *admission.spellAllowance, admission.counterspell, *receipt};
	entry->candidate.dest.clear(); // Store options/value, no borrowed creature views.
}
