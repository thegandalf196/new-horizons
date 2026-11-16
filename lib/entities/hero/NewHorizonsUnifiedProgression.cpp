/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsPerkState.h"
#include "NewHorizonsPerkOperation.h"
#include "NewHorizonsFourOfferProgression.h"
#include <algorithm>
#include <stdexcept>

namespace newHorizonsHeroes
{
bool usesUnifiedSkillProgression(const JsonNode & rules)
{
	return usesPerkRules(rules) && rules["unifiedSkillProgressionVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& rules["unifiedSkillProgressionVersion"].Integer() == 1;
}

void validateUnifiedSkillProgressionSerialization(const JsonNode & rules, bool supported)
{
	if(rules.isStruct() && rules.Struct().contains("unifiedSkillProgressionVersion") && !supported)
		throw std::runtime_error("Unified Skill progression requires the Leadership Efficiency save format");
}

bool PerkState::hasPerkForTier(const std::string & skillId, int tier) const
{
	return std::any_of(selected.begin(), selected.end(), [&](const PerkSelection & choice)
	{
		if(choice.skillId != skillId) return false;
		const auto definition = perkDefinition(rules, skillId, choice.perkId);
		return definition && perkRequiredRank(definition->requiredRank) == tier;
	});
}

std::vector<PerkOfferCandidate> PerkState::nextRequiredPerkOffer(
	const std::function<int(const std::string &)> & rankLookup) const
{
	if(!usesUnifiedSkillProgression(rules)) return {};
	return PerkSelectionOperation(*this).next(rankLookup);
}

void PerkState::selectMissingPerksDeterministically(const std::function<int(const std::string &)> & rankLookup)
{
	// The registry bounds completion to three selections per Skill. Work on a
	// copy so an invalid authored rank cannot partially initialize a hero.
	auto candidate = *this;
	for(size_t count = 0; count < PERK_CANONICAL_SKILL_COUNT * PERK_MAX_PERKS_PER_SKILL; ++count)
	{
		const auto offer = candidate.nextRequiredPerkOffer(rankLookup);
		if(offer.empty())
		{
			*this = std::move(candidate);
			return;
		}
		candidate.select(offer.front().selection.skillId, offer.front().selection.perkId,
			rankLookup(offer.front().selection.skillId));
	}
	if(!candidate.nextRequiredPerkOffer(rankLookup).empty())
		throw std::runtime_error("Mandatory perk completion exceeded the captured registry");
	*this = std::move(candidate);
}

bool PerkState::canAdvanceSkillNormally(std::string_view skillId, int currentRank) const
{
	if(currentRank < 0 || currentRank > 2)
		return false;
	if(!usesPerkRules(rules))
		return true;
	validate();
	for(int requiredTier = 1; requiredTier <= currentRank; ++requiredTier)
	{
		const bool tierSelected = std::any_of(selected.begin(), selected.end(), [&](const auto & entry)
		{
			const auto definition = perkDefinition(rules, entry.skillId, entry.perkId);
			return entry.skillId == skillId && definition
				&& perkRequiredRank(definition->requiredRank) == requiredTier;
		});
		if(!tierSelected)
			return false;
	}
	return !usesUnifiedSkillProgression(rules)
		|| !candidatesForSkillTier(std::string(skillId), currentRank + 1).empty();
}

std::vector<PerkOfferCandidate> PerkState::candidatesForSkillTier(const std::string & skillId, int tier) const
{
	if(!usesPerkRules(rules) || tier <= 0 || tier > 3)
		return {};
	return PerkSelectionOperation(*this).candidates(skillId, tier);
}

void PerkState::acceptOffer(const std::vector<PerkOfferCandidate> & offer, size_t choice,
	const std::function<int(const std::string &)> & rankLookup, uint64_t seed,
	const OfferWeightContext & weightContext)
{
	validate();
	if(offer.empty() || choice >= offer.size())
		throw std::runtime_error("Invalid New Horizons perk offer selection");
	const auto & selectedCandidate = offer[choice];
	if(usesFourOfferProgression(rules))
	{
		const auto rank = rankLookup(selectedCandidate.selection.skillId);
		if(offer != PerkSelectionOperation(*this).eligibleCandidates(selectedCandidate.selection.skillId, rank))
			throw std::runtime_error("Stale or forged four-offer perk pool");
		select(selectedCandidate.selection.skillId, selectedCandidate.selection.perkId, rank);
		return;
	}
	const auto tierPool = candidatesForSkillTier(selectedCandidate.selection.skillId, selectedCandidate.requiredRank);
	if(usesUnifiedSkillProgression(rules))
	{
		if(offer != tierPool || rankLookup(selectedCandidate.selection.skillId) < selectedCandidate.requiredRank)
			throw std::runtime_error("Stale or forged mandatory New Horizons perk offer");
	}
	else if(offer != tierPool && offer != prepareOffer(rankLookup, seed, weightContext))
		throw std::runtime_error("Stale or forged New Horizons perk offer");
	select(selectedCandidate.selection.skillId, selectedCandidate.selection.perkId,
		rankLookup(selectedCandidate.selection.skillId));
}

}
