/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsFourOfferProgression.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsPerkOperation.h"
#include "NewHorizonsRuleValidation.h"
#include "SkillChoiceEligibility.h"
#include "../../GameLibrary.h"
#include "../../CSkillHandler.h"
#include "../../callback/IGameInfoCallback.h"
#include "../../mapObjects/CGHeroInstance.h"
#include <stdexcept>
#include <set>

namespace newHorizonsHeroes
{
void validateHeroProgressionPolicy(const JsonNode & rules)
{
	using detail::require;
	if(rules.Struct().contains("fourOfferProgressionVersion"))
	{
		require(rules["fourOfferProgressionVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
			&& rules["fourOfferProgressionVersion"].Integer() == 1, "fourOfferProgressionVersion");
		require(!rules.Struct().contains("unifiedSkillProgressionVersion"), "conflicting hero progression policies");
		require(usesStrictPerkProgression(rules), "four-offer progression requires ordered perk prerequisites");
	}
	if(rules.Struct().contains("unifiedSkillProgressionVersion"))
	{
		require(rules["unifiedSkillProgressionVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
			&& rules["unifiedSkillProgressionVersion"].Integer() == 1, "unifiedSkillProgressionVersion");
		require(usesStrictPerkProgression(rules), "unified progression requires ordered one-per-tier perks");
	}
}

bool usesFourOfferProgression(const JsonNode & rules)
{
	return usesPerkRules(rules) && rules["fourOfferProgressionVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& rules["fourOfferProgressionVersion"].Integer() == 1;
}

bool usesPersistentHeroProgression(const JsonNode & rules)
{
	return usesUnifiedSkillProgression(rules) || usesFourOfferProgression(rules);
}

void validateFourOfferProgressionSerialization(const JsonNode & rules, bool supported)
{
	if(rules.isStruct() && rules.Struct().contains("fourOfferProgressionVersion") && !supported)
		throw std::runtime_error("Four-offer hero progression requires its successor save format");
}

void validateFourOfferSnapshot(const FourOfferProgressionOffer & offer, const PerkState & state)
{
	if(!usesFourOfferProgression(state.rules))
		throw std::runtime_error("Four-offer snapshot without captured policy");
	state.validate();
	const auto & catalogue = state.rules["skills"].Struct();
	const auto canonicalParent = [&](const std::string & identity)
	{
		const auto decoded = SecondarySkill::decode(identity);
		return decoded >= 0 && decoded < LIBRARY->skillh->size()
			&& SecondarySkill::encode(decoded) == identity && catalogue.contains(identity);
	};
	for(size_t slot = 0; slot < offer.skills.size(); ++slot)
	{
		const auto id = offer.skills[slot];
		if(id == SecondarySkill::NONE)
		{
			if(offer.targetRanks[slot] != 0) throw std::runtime_error("Missing four-offer Skill identity");
			continue;
		}
		if(id.getNum() < 0 || id.getNum() >= LIBRARY->skillh->size()
			|| !canonicalParent(SecondarySkill::encode(id.getNum()))
			|| (slot == 0 ? offer.targetRanks[slot] < 2 || offer.targetRanks[slot] > 3 : offer.targetRanks[slot] != 1))
			throw std::runtime_error("Invalid held four-offer Skill transition");
	}
	if(offer.skills[0] != SecondarySkill::NONE && offer.skills[0] == offer.skills[1])
		throw std::runtime_error("Duplicate held four-offer Skills");
	std::set<std::string> groups;
	for(const auto & parent : offer.perkSkills)
	{
		if(parent.empty()) continue;
		if(!canonicalParent(parent) || !groups.insert(parent).second)
			throw std::runtime_error("Invalid held four-offer perk parent");
	}
	if(offer.perkSkills[0].empty() && !offer.perkSkills[1].empty())
		throw std::runtime_error("Alternative perk group without a primary group");
}

std::vector<FourOfferSkillCandidate> fourOfferCandidates(const IGameInfoCallback & info, const CGHeroInstance & hero)
{
	if(!usesFourOfferProgression(hero.getPerkState().rules))
		throw std::runtime_error("Four-offer candidates without captured policy");
	const auto & state = hero.getPerkState();
	PerkSelectionOperation operation(state);
	const auto weightContext = hero.getPerkOfferWeightContext();
	std::vector<FourOfferSkillCandidate> result;
	for(const auto & definition : LIBRARY->skillh->objects)
	{
		if(!definition) continue;
		const auto skill = definition->getId();
		const auto & skillId = definition->getJsonKey();
		const auto & development = hero.getPrimaryGrowthRules();
		if(isExcludedSkill(development, skill)
			|| (isFactionSkill(development, skill) && !isFactionSkillForFaction(development, hero.getFactionID(), skill)))
			continue;
		const int rank = hero.getSecSkillLevel(skill);
		const auto weight = weightContext.lookup ? weightContext.lookup(skillId) : std::nullopt;
		FourOfferSkillCandidate candidate{skill, rank, weight.value_or(0), false, {}};
		candidate.skillEligible = candidate.weight > 0
			&& !isExcludedSkill(hero.getPrimaryGrowthRules(), skill)
			&& isValidLevelUpSkillChoice(info, &hero, skill);
		if(rank > 0) candidate.perks = operation.eligibleCandidates(skillId, rank);
		if(candidate.skillEligible || !candidate.perks.empty()) result.push_back(std::move(candidate));
	}
	return result;
}
}
