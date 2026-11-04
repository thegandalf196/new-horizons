/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ArrowContinuationReuse.h"
#include "HeroCandidateValueContext.h"
#include "VinesCandidateValueReuse.h"
#include "BattleExchangeVariant.h"
#include <cmath>

namespace arrowContinuation
{
bool sourcesSupported(const HypotheticBattle & state, uint32_t target) noexcept
{
	try
	{
		if(!state.hasCanonicalDamageQuerySources()) return false;
		const auto * unit = state.battleGetUnitByID(target);
		if(!unit) return false;
		heroActionProjection::CanonicalSourceAdmission source(*unit, state);
		return source.admits(*unit); // Existing whole-roster graph proof, fresh after each cast.
	}
	catch(...) { return false; }
}
bool scoringSupported(const CBattleInfoCallback & state) noexcept
{
	try { return VinesCandidateValueReuse::scoringScriptsSupported(state); }
	catch(...) { return false; } // Only optional implementation admission.
}
std::optional<heroActionUtility::AbsoluteContinuationUtility> Reuse::find(
	const PossibleSpellcast & candidate, const heroActionProjection::Admission & admission,
	const std::vector<uint32_t> & recipients, const std::optional<Outcome> & outcome,
	const HypotheticBattle & state, bool full)
{
	admitted = false;
	if((control && !control->enabled) || !full || !isArrow(candidate) || !outcome
		|| !admission.admitted || !admission.spellAllowance || admission.counterspellNegated
		|| admission.counterspell.wardActive || !admission.counterspell.resolutionKnown
		|| !admission.counterspell.negated || *admission.counterspell.negated
		|| !sourcesSupported(state, outcome->target) || !resourceIndependent(state)
		|| !scoringSupported(state))
	{
		clear();
		return std::nullopt;
	}
	admitted = true;
	// Deliberate Arrow-only normalization. Shared sameOptions retains overcharge.
	auto options = candidate;
	options.spellOvercharge = 0;
	for(const auto & entry : entries)
		if(entry.allowance == *admission.spellAllowance
			&& heroCandidateValueContext::sameCounterspell(entry.counterspell, admission.counterspell)
			&& entry.recipients == recipients
			&& entry.outcome == *outcome && heroCandidateValueContext::sameOptions(entry.options, options))
		{
			if(control) ++control->hits;
			return entry.value;
		}
	return std::nullopt;
}
void Reuse::remember(const PossibleSpellcast & candidate, const heroActionProjection::Admission & admission,
	const std::vector<uint32_t> & recipients, const std::optional<Outcome> & outcome,
	heroActionUtility::AbsoluteContinuationUtility value, const HypotheticBattle & state)
{
	if(!admitted || (control && !control->enabled) || !outcome || !admission.spellAllowance
		|| !std::isfinite(value.value) || value.value == std::numeric_limits<float>::lowest()
		|| value.value == static_cast<float>(EvaluationResult::INEFFECTIVE_SCORE)
		|| !sourcesSupported(state, outcome->target) || !resourceIndependent(state) || !scoringSupported(state))
	{
		clear();
		return;
	}
	auto options = candidate;
	options.spellOvercharge = 0;
	entries.push_back({std::move(options), *admission.spellAllowance, admission.counterspell, recipients, *outcome, value});
}
}
