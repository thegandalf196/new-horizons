/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../lib/entities/hero/NewHorizonsPerkOperation.h"
#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace perkTestSetup
{
/// Effect fixtures arrange one currently legal perk without pretending to
/// generate a level-up. Actual weighted four-slot choices have dedicated
/// server/UI/AI tests. Historical captures retain their original sampler.
inline std::vector<newHorizonsHeroes::PerkOfferCandidate> forSkill(
	const newHorizonsHeroes::PerkState & state, std::string_view skill,
	const std::function<int(const std::string &)> & rankLookup, uint64_t seed,
	const newHorizonsHeroes::PerkState::OfferWeightContext & weights = newHorizonsHeroes::PerkState::OfferWeightContext())
{
	if(!newHorizonsHeroes::usesFourOfferProgression(state.getRules()))
		return state.prepareOffer(rankLookup, seed, weights);
	const std::string identity(skill);
	return newHorizonsHeroes::PerkSelectionOperation(state).eligibleCandidates(identity, rankLookup(identity));
}

inline std::vector<newHorizonsHeroes::PerkOfferCandidate> forPerk(
	const newHorizonsHeroes::PerkState & state, std::string_view perk,
	const std::function<int(const std::string &)> & rankLookup, uint64_t seed,
	const newHorizonsHeroes::PerkState::OfferWeightContext & weights = newHorizonsHeroes::PerkState::OfferWeightContext())
{
	const auto delimiter = perk.find('.');
	if(newHorizonsHeroes::usesFourOfferProgression(state.getRules()) && delimiter == std::string_view::npos)
		throw std::runtime_error("Effect fixture requires a scoped perk identity");
	return forSkill(state, perk.substr(0, delimiter), rankLookup, seed, weights);
}

template<typename Predicate>
std::vector<newHorizonsHeroes::PerkOfferCandidate> matching(
	const newHorizonsHeroes::PerkState & state, const Predicate & predicate,
	const std::function<int(const std::string &)> & rankLookup, uint64_t seed,
	const newHorizonsHeroes::PerkState::OfferWeightContext & weights = newHorizonsHeroes::PerkState::OfferWeightContext())
{
	if(!newHorizonsHeroes::usesFourOfferProgression(state.getRules()))
		return state.prepareOffer(rankLookup, seed, weights);
	newHorizonsHeroes::PerkSelectionOperation operation(state);
	for(const auto & [skill, definition] : state.getRules()["skills"].Struct())
	{
		auto candidates = operation.eligibleCandidates(skill, rankLookup(skill));
		if(std::any_of(candidates.begin(), candidates.end(), predicate)) return candidates;
	}
	return {};
}

inline uint64_t seedLimit(const newHorizonsHeroes::PerkState & state, uint64_t historicalLimit)
{
	return newHorizonsHeroes::usesFourOfferProgression(state.getRules()) ? 1 : historicalLimit;
}
}
