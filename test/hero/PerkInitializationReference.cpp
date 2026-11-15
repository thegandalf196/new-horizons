/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PerkInitializationReference.h"
#include "../../lib/entities/hero/NewHorizonsRuleValidation.h"
#include <algorithm>
#include <map>
#include <set>
// Pre-optimization traversal retained independently of PerkSelectionOperation.
// Raw selection legality is also exercised by the existing PerkState suite.
namespace perkInitializationReference
{
using namespace newHorizonsHeroes;
namespace
{

using TierKey = std::pair<std::string, int>;
using Preferences = std::map<TierKey, PerkSelection>;
using detail::fields;
using detail::integer;
using detail::require;
using detail::resolve;

int selectedTier(const PerkState & state, const PerkSelection & choice)
{
	const auto definition = perkDefinition(state.rules, choice.skillId, choice.perkId);
	require(definition && definition->effect["status"].String() == "active",
		"active authored starting perk " + choice.perkId);
	const int tier = perkRequiredRank(definition->requiredRank);
	require(tier >= 1 && tier <= 3, "authored starting perk tier");
	return tier;
}

void appendPreferences(Preferences & preferences, const PerkState & state,
	const std::vector<PerkSelection> & choices)
{
	std::set<TierKey> seen;
	for(const auto & choice : choices)
	{
		const TierKey key{choice.skillId, selectedTier(state, choice)};
		require(seen.insert(key).second, "duplicate authored starting perk tier");
		// Existing state and earlier explicit defaults take precedence. These
		// preferences never grant a rank or a perk for an absent parent Skill.
		preferences.try_emplace(key, choice);
	}
}

std::vector<PerkSelection> defaultsFor(const JsonNode & development, HeroTypeID hero)
{
	if(hero == HeroTypeID::NONE) return {};
	const auto & table = development["startingSkills"]["startingPerkProfiles"].Struct();
	const auto found = table.find(HeroTypeID::encode(hero.getNum()));
	if(found == table.end()) return {};
	std::vector<PerkSelection> result;
	for(const auto & entry : found->second.Vector())
		result.push_back({entry["skill"].String(), entry["perk"].String()});
	return result;
}
}

namespace
{
std::vector<PerkOfferCandidate> referenceCandidates(const PerkState & state, const std::string & skillId, int tier)
{
	if(!usesPerkRules(state.rules) || tier <= 0 || tier > 3)
		return {};
	state.validate();
	if(usesUnifiedSkillProgression(state.rules) && state.hasPerkForTier(skillId, tier))
		return {};
	std::vector<PerkOfferCandidate> result;
	if(!state.rules["skills"].isStruct())
		return {};
	const auto & skillsMap = state.rules["skills"].Struct();
	const auto it = skillsMap.find(skillId);
	if(it == skillsMap.end())
		return {};
	const auto & skillNode = it->second;
	if(!skillNode["perks"].isVector())
		return {};
	for(const auto & perkNode : skillNode["perks"].Vector())
	{
		if(perkNode["effect"]["status"].String() != "active")
			continue;
		const auto & perkId = perkNode["id"].String();
		if(state.hasSelection(skillId, perkId))
			continue;
		const int requiredRank = perkRequiredRank(perkNode["requires"].String());
		if(requiredRank != tier)
			continue;
		if(usesUnifiedSkillProgression(state.rules) && !state.canAcquirePerk(skillId, perkId, tier))
			continue;
		result.push_back(PerkOfferCandidate{
			{skillId, perkId},
			perkNode["name"].String(),
			perkNode["description"].String(),
			requiredRank
		});
	}
	return result;
}
std::vector<PerkOfferCandidate> referenceNext(const PerkState & state,
	const std::function<int(const std::string &)> & rankLookup)
{
	if(!usesUnifiedSkillProgression(state.rules)) return {};
	state.validate();
	for(const auto & [skillId, definition] : state.rules["skills"].Struct())
	{
		const int rank = rankLookup(skillId);
		if(rank < 0 || rank > 3)
			throw std::runtime_error("Invalid Skill rank during mandatory perk reconciliation");
		for(int tier = 1; tier <= rank; ++tier)
		{
			if(state.hasPerkForTier(skillId, tier)) continue;
			auto result = referenceCandidates(state, skillId, tier);
			if(result.empty())
				throw std::runtime_error("Acquired Skill rank has no eligible mandatory perk");
			return result;
		}
	}
	return {};
}
}
void initialize(PerkState & state, const JsonNode & development, HeroTypeID hero,
	const std::vector<PerkSelection> & profile, const std::vector<PerkSelection> & prototype,
	const std::function<int(const std::string &)> & rank)
{
	if(!usesAuthoredStartingPerks(development))
	{
		selectStartingHeroPerks(state, profile, prototype, rank);
		return;
	}
	validateStartingPerkProfiles(development["startingSkills"]);
	require(usesUnifiedSkillProgression(state.rules), "starting defaults require unified Skill progression");
	auto proposed = state;
	proposed.validate();
	for(const auto & choice : proposed.selected)
		require(rank(choice.skillId) >= selectedTier(proposed, choice), "owned authored starting perk rank");
	Preferences preferred;
	appendPreferences(preferred, proposed, prototype);
	appendPreferences(preferred, proposed, profile);
	appendPreferences(preferred, proposed, defaultsFor(development, hero));
	for(size_t count = 0; count < PERK_CANONICAL_SKILL_COUNT * PERK_MAX_PERKS_PER_SKILL; ++count)
	{
		const auto offer = referenceNext(proposed, rank);
		if(offer.empty())
		{
			state = std::move(proposed);
			return;
		}
		const auto & first = offer.front();
		const auto preference = preferred.find({first.selection.skillId, first.requiredRank});
		const auto choice = preference == preferred.end() ? offer.end()
			: std::ranges::find_if(offer, [&](const auto & candidate)
				{ return candidate.selection == preference->second; });
		const auto & selected = choice == offer.end() ? first.selection : choice->selection;
		proposed.select(selected.skillId, selected.perkId, rank(selected.skillId));
	}
	if(!referenceNext(proposed, rank).empty())
		throw std::runtime_error("Starting perk completion exceeded the captured registry");
	state = std::move(proposed);
}
}
