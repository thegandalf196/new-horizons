/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsStartingPerks.h"
#include "HeroStartingProjection.h"
#include "NewHorizonsRuleValidation.h"
#include <map>
#include <set>

namespace newHorizonsHeroes
{
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

bool usesAuthoredStartingPerks(const JsonNode & development)
{
	const auto & starts = development["startingSkills"];
	return usesRules(development) && starts["startingPerksPolicyVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& starts["startingPerksPolicyVersion"].Integer() == 1;
}

void validateStartingPerkProfiles(const JsonNode & starts)
{
	require(starts.isStruct(), "starting Skill policy object");
	const bool marker = starts.Struct().contains("startingPerksPolicyVersion");
	const bool table = starts.Struct().contains("startingPerkProfiles");
	if(!marker && !table) return;
	require(marker && table && starts["startingPerksPolicyVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& integer(starts["startingPerksPolicyVersion"], 1, 1),
		"authored starting perk policy and table required together");
	const auto & profiles = starts["startingPerkProfiles"];
	require(profiles.isStruct() && !profiles.Struct().empty(), "authored starting perk profiles");
	std::set<int> heroes;
	for(const auto & [hero, choices] : profiles.Struct())
	{
		const int id = resolve(HeroTypeID::entityType(), hero);
		require(HeroTypeID::encode(id) == hero && heroes.insert(id).second,
			"canonical unique starting perk hero");
		require(choices.isVector() && !choices.Vector().empty()
			&& choices.Vector().size() <= PERK_CANONICAL_SKILL_COUNT * PERK_MAX_PERKS_PER_SKILL,
			"bounded authored starting perk choices");
		std::set<std::pair<std::string, std::string>> seen;
		for(const auto & choice : choices.Vector())
		{
			fields(choice, {"skill", "perk"});
			require(choice["skill"].isString() && choice["perk"].isString(), "starting perk identifiers");
			const auto & skill = choice["skill"].String();
			const auto & perk = choice["perk"].String();
			const int parent = resolve(SecondarySkill::entityType(), skill);
			require(SecondarySkill::encode(parent) == skill && perk.starts_with(skill + ".")
				&& seen.emplace(skill, perk).second, "canonical unique authored starting perk");
		}
	}
}

void initializeStartingHeroPerks(PerkState & state, const JsonNode & development, HeroTypeID hero,
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
		const auto offer = proposed.nextRequiredPerkOffer(rank);
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
	if(!proposed.nextRequiredPerkOffer(rank).empty())
		throw std::runtime_error("Starting perk completion exceeded the captured registry");
	state = std::move(proposed);
}
}
