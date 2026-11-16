/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsFourOfferProgression.h"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace newHorizonsHeroes
{
namespace
{
using Pool = std::vector<const FourOfferSkillCandidate *>;

Pool filter(const std::vector<FourOfferSkillCandidate> & candidates,
	const std::function<bool(const FourOfferSkillCandidate &)> & eligible)
{
	Pool result;
	for(const auto & candidate : candidates)
		if(eligible(candidate)) result.push_back(&candidate);
	return result;
}

Pool highestRank(Pool candidates)
{
	if(candidates.empty()) return candidates;
	const int rank = (*std::max_element(candidates.begin(), candidates.end(), [](const auto * left, const auto * right)
		{ return left->rank < right->rank; }))->rank;
	std::erase_if(candidates, [rank](const auto * candidate) { return candidate->rank != rank; });
	return candidates;
}

Pool skillPool(const std::vector<FourOfferSkillCandidate> & candidates, size_t slot)
{
	auto pool = filter(candidates, [slot](const auto & candidate)
	{
		return candidate.skillEligible && candidate.weight > 0
			&& (slot == 0 ? candidate.rank > 0 && candidate.rank < 3 : candidate.rank == 0);
	});
	return slot == 0 ? highestRank(std::move(pool)) : pool;
}

Pool perkPool(const std::vector<FourOfferSkillCandidate> & candidates, size_t slot,
	SecondarySkill excluded = SecondarySkill::NONE)
{
	auto pool = filter(candidates, [excluded](const auto & candidate)
	{
		return candidate.skill != excluded && candidate.rank > 0 && !candidate.perks.empty();
	});
	return slot == 0 ? highestRank(std::move(pool)) : pool;
}

const FourOfferSkillCandidate * weightedChoice(const Pool & pool, bool perk,
	const FourOfferWeightedChooser & choose)
{
	if(pool.empty()) return nullptr;
	std::vector<int> weights;
	weights.reserve(pool.size());
	for(const auto * candidate : pool)
		weights.push_back(perk ? std::max(1, candidate->weight) : candidate->weight);
	const auto selected = choose(weights);
	if(selected >= pool.size()) throw std::runtime_error("Invalid four-offer weighted draw");
	return pool[selected];
}

void validatePool(const std::vector<FourOfferSkillCandidate> & candidates)
{
	std::set<SecondarySkill> skills;
	for(const auto & candidate : candidates)
	{
		if(candidate.skill == SecondarySkill::NONE || candidate.rank < 0 || candidate.rank > 3
			|| candidate.weight < 0 || !skills.insert(candidate.skill).second)
			throw std::runtime_error("Invalid four-offer candidate snapshot");
	}
}

const FourOfferSkillCandidate * find(const Pool & pool, SecondarySkill skill)
{
	const auto found = std::find_if(pool.begin(), pool.end(), [skill](const auto * candidate)
		{ return candidate->skill == skill; });
	return found == pool.end() ? nullptr : *found;
}


}

std::optional<std::vector<PerkOfferCandidate>> progressionSpecificPerkOffer(
	const PerkState & state, const std::function<int(const std::string &)> & rankLookup)
{
	if(usesFourOfferProgression(state.rules)) return std::vector<PerkOfferCandidate>{};
	if(usesUnifiedSkillProgression(state.rules)) return state.nextRequiredPerkOffer(rankLookup);
	return std::nullopt;
}

bool FourOfferProgressionOffer::empty() const
{
	return skills[0] == SecondarySkill::NONE && skills[1] == SecondarySkill::NONE
		&& targetRanks[0] == 0 && targetRanks[1] == 0 && perkSkills[0].empty() && perkSkills[1].empty();
}

FourOfferProgressionOffer prepareFourOfferProgression(
	const std::vector<FourOfferSkillCandidate> & candidates, const FourOfferWeightedChooser & choose)
{
	validatePool(candidates);
	FourOfferProgressionOffer result;
	for(size_t slot = 0; slot < result.skills.size(); ++slot)
		if(const auto * selected = weightedChoice(skillPool(candidates, slot), false, choose))
		{
			result.skills[slot] = selected->skill;
			result.targetRanks[slot] = selected->rank + 1;
		}
	SecondarySkill excluded = SecondarySkill::NONE;
	for(size_t slot = 0; slot < result.perkSkills.size(); ++slot)
		if(const auto * selected = weightedChoice(perkPool(candidates, slot, excluded), true, choose))
		{
			result.perkSkills[slot] = selected->perks.front().selection.skillId;
			excluded = selected->skill;
		}
	return result;
}

void validateFourOfferProgression(const FourOfferProgressionOffer & offer,
	const std::vector<FourOfferSkillCandidate> & candidates)
{
	validatePool(candidates);
	for(size_t slot = 0; slot < offer.skills.size(); ++slot)
	{
		const auto pool = skillPool(candidates, slot);
		if(offer.skills[slot] == SecondarySkill::NONE ? !pool.empty() : !find(pool, offer.skills[slot]))
			throw std::runtime_error("Stale or forged four-offer Skill slot");
		const auto * candidate = find(pool, offer.skills[slot]);
		if(offer.targetRanks[slot] != (candidate ? candidate->rank + 1 : 0))
			throw std::runtime_error("Invalid issued four-offer Skill rank");
	}
	SecondarySkill excluded = SecondarySkill::NONE;
	for(size_t slot = 0; slot < offer.perkSkills.size(); ++slot)
	{
		const auto pool = perkPool(candidates, slot, excluded);
		const auto & parent = offer.perkSkills[slot];
		if(parent.empty())
		{
			if(!pool.empty()) throw std::runtime_error("Missing four-offer perk group");
			continue;
		}
		const auto selected = std::find_if(pool.begin(), pool.end(), [&](const auto * candidate)
			{ return candidate->perks.front().selection.skillId == parent; });
		if(selected == pool.end()) throw std::runtime_error("Invalid issued four-offer perk parent");
		excluded = (*selected)->skill;
	}
}

FourOfferPerkGroups fourOfferPerkGroups(const FourOfferProgressionOffer & offer,
	const std::vector<FourOfferSkillCandidate> & liveCandidates)
{
	FourOfferPerkGroups result;
	for(size_t slot = 0; slot < offer.perkSkills.size(); ++slot)
		for(const auto & candidate : liveCandidates)
			if(!candidate.perks.empty() && candidate.perks.front().selection.skillId == offer.perkSkills[slot])
				result[slot] = candidate.perks;
	return result;
}

int32_t fourOfferPerkReply(const FourOfferProgressionOffer & offer, const PerkState & state,
	size_t group, const std::string & perkId)
{
	if(group >= offer.perkSkills.size() || offer.perkSkills[group].empty())
		throw std::runtime_error("Invalid four-offer perk group");
	const auto & catalogue = state.rules["skills"][offer.perkSkills[group]]["perks"].Vector();
	for(size_t index = 0; index < catalogue.size() && index < PERK_CANONICAL_POOL_SIZE; ++index)
		if(catalogue[index]["id"].String() == perkId)
			return static_cast<int32_t>(2 + group * PERK_CANONICAL_POOL_SIZE + index);
	throw std::runtime_error("Perk does not belong to the held four-offer group");
}

std::vector<int32_t> legalFourOfferReplies(const FourOfferProgressionOffer & offer, const PerkState & state,
	const std::vector<FourOfferSkillCandidate> & liveCandidates)
{
	std::vector<int32_t> result;
	for(size_t slot = 0; slot < offer.skills.size(); ++slot)
		for(const auto & candidate : liveCandidates)
			if(candidate.skill == offer.skills[slot] && candidate.skillEligible && candidate.weight > 0
				&& candidate.rank + 1 == offer.targetRanks[slot]
				&& (slot == 0 ? candidate.rank > 0 && candidate.rank < 3 : candidate.rank == 0))
				result.push_back(static_cast<int32_t>(slot));
	const auto groups = fourOfferPerkGroups(offer, liveCandidates);
	for(size_t group = 0; group < groups.size(); ++group)
		for(const auto & perk : groups[group])
			result.push_back(fourOfferPerkReply(offer, state, group, perk.selection.perkId));
	return result;
}

std::optional<FourOfferProgressionSelection> fourOfferSelection(const FourOfferProgressionOffer & offer,
	const PerkState & state, int32_t reply)
{
	if(reply < 0) return std::nullopt;
	if(offer.empty())
		return reply == 0 ? std::optional(FourOfferProgressionSelection{SecondarySkill::NONE, std::nullopt, true}) : std::nullopt;
	if(reply < 2)
		return offer.skills[reply] != SecondarySkill::NONE
			? std::optional(FourOfferProgressionSelection{offer.skills[reply], std::nullopt, false}) : std::nullopt;
	const size_t index = static_cast<size_t>(reply - 2);
	const size_t group = index / PERK_CANONICAL_POOL_SIZE;
	if(group >= offer.perkSkills.size() || offer.perkSkills[group].empty()) return std::nullopt;
	const auto parent = state.rules["skills"].Struct().find(offer.perkSkills[group]);
	if(parent == state.rules["skills"].Struct().end()) return std::nullopt;
	const auto & catalogue = parent->second["perks"].Vector();
	const size_t perkIndex = index % PERK_CANONICAL_POOL_SIZE;
	if(perkIndex >= catalogue.size()) return std::nullopt;
	const auto & perk = catalogue[perkIndex];
	return FourOfferProgressionSelection{SecondarySkill::NONE, PerkOfferCandidate{{offer.perkSkills[group], perk["id"].String()},
		perk["name"].String(), perk["description"].String(), perkRequiredRank(perk["requires"].String())}, false};
}

std::vector<FourOfferSkillCandidate> fourOfferRerollCandidates(const FourOfferProgressionOffer & offer,
	const std::vector<FourOfferSkillCandidate> & candidates, size_t slot)
{
	if(slot >= offer.skills.size() || offer.skills[slot] == SecondarySkill::NONE) return {};
	std::vector<FourOfferSkillCandidate> result;
	for(const auto * candidate : skillPool(candidates, slot))
		if(std::find(offer.skills.begin(), offer.skills.end(), candidate->skill) == offer.skills.end())
			result.push_back(*candidate);
	return result;
}

bool perkChoiceBlocksTierCompletion(const PerkState & state, const PerkOfferCandidate & candidate, int currentRank)
{
	const auto blockedSlots = [&](const PerkState & value)
	{
		int count = 0;
		for(const auto & selection : value.selected)
			if(selection.skillId == candidate.selection.skillId) ++count;
		for(int tier = 1; tier <= 3; ++tier)
			if(!value.hasPerkForTier(candidate.selection.skillId, tier)) ++count;
		return std::max(0, count - PERK_MAX_PERKS_PER_SKILL);
	};
	auto proposed = state;
	proposed.select(candidate.selection.skillId, candidate.selection.perkId, currentRank);
	return blockedSlots(proposed) > blockedSlots(state);
}
}
