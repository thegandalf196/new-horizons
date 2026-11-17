/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "SearchBudget.h"
#include "HeroDecisionPressure.h"
#include "../../lib/battle/Unit.h"
class CSpell;
class PossibleSpellcast;
namespace battleSearch
{
/// One lazy reference queue per Hero decision; borrowed units stay owned by its
/// callback. This is neither a new search horizon nor an independent planner.
class RegenerationTurnQueue
{
	std::shared_ptr<CBattleInfoCallback> source;
	std::shared_ptr<SearchBudget> work;
	std::vector<battle::Units> queue;
	bool ready = false;
public:
	RegenerationTurnQueue(std::shared_ptr<CBattleInfoCallback> source, std::shared_ptr<SearchBudget> work)
		: source(std::move(source)), work(std::move(work)) {}
	bool prepared() const { return ready; }
	const std::vector<battle::Units> & get();
};

/// Decision policy owns work reservations; native mechanics retain legality,
/// projection and utility. No state or candidate pointer escapes this object.
class HeroCandidateSearch
{
	std::shared_ptr<SearchBudget> root;
	SearchLimits limits;
	std::optional<HeroWork> continuations;
	FamilyReservations generation;
	heroDecisionPressure::Experiment fallbackExperiment;
	std::vector<size_t> plan;
public:
	class SpellWork
	{
		friend class HeroCandidateSearch;
		std::optional<SearchBudget> targets;
		std::optional<SearchBudget> preparation;
		bool admitted = true;
	public:
		explicit operator bool() const { return admitted; }
		SearchBudget * targetBudget() { return targets ? &*targets : nullptr; }
		bool consumePreparation() { return !preparation || preparation->consume(WorkKind::Generation); }
	};
	HeroCandidateSearch(std::shared_ptr<SearchBudget> root, const std::optional<SearchLimits> & policy);
	bool budgeted() const { return root != nullptr; }
	bool scoutPlan() const { return budgeted() && limits.refineScouted; }
	static Family family(const CSpell * spell);
	SpellWork prepareSpell(const CSpell * spell);
	SearchBudget * orderGenerationBudget() { return generation.forFamily(Family::Orders); }
	heroDecisionPressure::Experiment * experiment(heroDecisionPressure::Experiment * requested);
	void preparePlan(const std::vector<PossibleSpellcast> & candidates, const heroDecisionPressure::Experiment * experiment);
	std::shared_ptr<SearchBudget> beginCandidate(size_t ordinal);
};
}
