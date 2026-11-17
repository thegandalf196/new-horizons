/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "HeroCandidateSearch.h"
#include "PossibleSpellcast.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include <limits>
#include "../../lib/spells/CSpell.h"
#include <algorithm>
namespace battleSearch
{
const std::vector<battle::Units> & RegenerationTurnQueue::get()
{
	if(!ready)
	{
		if(work)
		{
			const uint64_t units = source->battleGetAllUnits(false).size();
			const uint64_t workUnits = units * 4;
			if(workUnits > std::numeric_limits<uint32_t>::max()) throw BudgetExhausted(WorkKind::Node);
			work->require(WorkKind::Node, static_cast<uint32_t>(workUnits));
		}
		source->battleGetTurnOrder(queue, 0, 4);
		ready = true;
	}
	return queue;
}

HeroCandidateSearch::HeroCandidateSearch(std::shared_ptr<SearchBudget> root, const std::optional<SearchLimits> & policy)
	: root(std::move(root)), limits(policy.value_or(SearchLimits::full())),
	  continuations(this->root ? std::optional<HeroWork>(std::in_place, *this->root, limits) : std::nullopt),
	  generation(this->root.get(), WorkKind::Generation, limits.generationPerFamily, limits.refineScouted)
{}

Family HeroCandidateSearch::family(const CSpell * spell)
{
	return spell ? spellFamily(spell->getJsonKey(), spell->isDamage(), spell->isPositive(), false) : Family::Control;
}

HeroCandidateSearch::SpellWork HeroCandidateSearch::prepareSpell(const CSpell * spell)
{
	SpellWork result;
	if(!root) return result; // Explicit legacy diagnostic only.
	auto * source = generation.forFamily(family(spell));
	WorkAmounts quota{};
	quota[0] = std::min(limits.generationPerSpell, source->available()[0] / 2);
	result.targets = source->reserve(quota);
	result.preparation = source->reserve(quota);
	result.admitted = result.targets.has_value() && result.preparation.has_value();
	return result;
}

heroDecisionPressure::Experiment * HeroCandidateSearch::experiment(heroDecisionPressure::Experiment * requested)
{
	if(!scoutPlan()) return requested;
	if(!requested) requested = &fallbackExperiment;
	if(!requested->refinementLimit) requested->refinementLimit = limits.refinementCount;
	return requested;
}

void HeroCandidateSearch::preparePlan(const std::vector<PossibleSpellcast> & casts,
	const heroDecisionPressure::Experiment * experiment)
{
	if(!root) return;
	std::vector<std::optional<float>> estimates(casts.size());
	if(experiment)
		for(const auto & estimate : experiment->cheapScores) estimates.at(estimate.ordinal) = estimate.marginal;
	std::vector<RefinementCandidate> candidates;
	candidates.reserve(casts.size());
	for(size_t ordinal = 0; ordinal != casts.size(); ++ordinal)
	{
		const auto & cast = casts[ordinal];
		const auto group = cast.command != HeroCommand::NONE ? Family::Orders : family(cast.spell);
		candidates.push_back({ordinal, group, estimates[ordinal]});
	}
	plan = refinementPlan(candidates, limits);
}

std::shared_ptr<SearchBudget> HeroCandidateSearch::beginCandidate(size_t ordinal)
{
	if(!root || !std::binary_search(plan.begin(), plan.end(), ordinal)) return {};
	auto quota = continuations->reserveContinuation();
	if(!quota) return {};
	auto result = std::make_shared<SearchBudget>(std::move(*quota));
	result->require(WorkKind::Continuation);
	return result;
}
}
