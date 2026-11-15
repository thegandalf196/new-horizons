/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../network/InitializationProfile.h"
#include "NewHorizonsStartingPerks.h"
#include "NewHorizonsPerkOperation.h"
#include "HeroStartingProjection.h"
#include "NewHorizonsRuleValidation.h"
#include "CHeroHandler.h"
#include "CHeroClass.h"
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

int selectedTier(const ValidatedPerkRules & catalog, const PerkSelection & choice)
{
	const auto definition = catalog.definition(choice.skillId, choice.perkId);
	require(definition && definition->effect["status"].String() == "active",
		"active authored starting perk " + choice.perkId);
	const int tier = perkRequiredRank(definition->requiredRank);
	require(tier >= 1 && tier <= 3, "authored starting perk tier");
	return tier;
}

void appendPreferences(Preferences & preferences, const ValidatedPerkRules & catalog,
	const std::vector<PerkSelection> & choices)
{
	std::set<TierKey> seen;
	for(const auto & choice : choices)
	{
		const TierKey key{choice.skillId, selectedTier(catalog, choice)};
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
		&& integer(starts["startingPerksPolicyVersion"], 1, 2);
}

bool usesBasicStartingProfiles(const JsonNode & starts)
{
	return starts["startingPerksPolicyVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& starts["startingPerksPolicyVersion"].Integer() == 2;
}

void validateStartingPerkProfiles(const JsonNode & starts)
{
	nhLoadProfile::Scope measured(nhLoadProfile::Metric::ProfileValidation);
	require(starts.isStruct(), "starting Skill policy object");
	const bool marker = starts.Struct().contains("startingPerksPolicyVersion");
	const bool table = starts.Struct().contains("startingPerkProfiles");
	if(!marker && !table) return;
	require(marker && table && starts["startingPerksPolicyVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& integer(starts["startingPerksPolicyVersion"], 1, 2),
		"authored starting perk policy and table required together");
	const auto & profiles = starts["startingPerkProfiles"];
	require(profiles.isStruct() && !profiles.Struct().empty(), "authored starting perk profiles");
	const bool basic = usesBasicStartingProfiles(starts);
	const auto & development = starts["startingDevelopmentProfiles"];
	if(basic)
	{
		require(development.isStruct(), "Basic defaults require development profiles");
		require(starts["factionSkills"].isStruct(), "Basic defaults require faction Skill map");
		// Core registrations include campaign and portrait heroes. Mod helpers
		// are independent content and do not become mandatory default rows.
		for(const auto & hero : LIBRARY->heroh->objects)
			if(hero && HeroTypeID::encode(hero->getId().getNum()).starts_with("core:"))
			{
				const auto identity = HeroTypeID::encode(hero->getId().getNum());
				require(profiles.Struct().contains(identity) && development.Struct().contains(identity),
					"complete registered Core starting profiles " + identity);
			}
	}
	std::set<int> heroes;
	if(nhLoadProfile::active())
		nhLoadProfile::count(nhLoadProfile::Metric::ProfileRows, profiles.Struct().size());
	for(const auto & [hero, choices] : profiles.Struct())
	{
		const int id = resolve(HeroTypeID::entityType(), hero);
		require(HeroTypeID::encode(id) == hero && heroes.insert(id).second,
			"canonical unique starting perk hero");
		require(choices.isVector() && !choices.Vector().empty()
			&& choices.Vector().size() <= PERK_CANONICAL_SKILL_COUNT * PERK_MAX_PERKS_PER_SKILL,
			"bounded authored starting perk choices");
		if(basic)
			require(choices.Vector().size() == 2 && development.Struct().contains(hero),
				"exactly two Basic starting perk parents");
		if(nhLoadProfile::active())
			nhLoadProfile::count(nhLoadProfile::Metric::ProfileChoices, choices.Vector().size());
		std::set<std::pair<std::string, std::string>> seen;
		std::set<std::string> parents;
		for(const auto & choice : choices.Vector())
		{
			fields(choice, {"skill", "perk"});
			require(choice["skill"].isString() && choice["perk"].isString(), "starting perk identifiers");
			const auto & skill = choice["skill"].String();
			const auto & perk = choice["perk"].String();
			const int parent = resolve(SecondarySkill::entityType(), skill);
			require(SecondarySkill::encode(parent) == skill && perk.starts_with(skill + ".")
				&& seen.emplace(skill, perk).second, "canonical unique authored starting perk");
			if(basic)
				require(parents.insert(skill).second, "distinct Basic starting perk parents");
		}
		if(basic)
		{
			const auto & profile = development[hero];
			require(profile.isStruct(), "Basic starting development row object");
			const auto & skills = profile["skills"];
			require(skills.isVector() && skills.Vector().size() == 2, "two Basic starting parents");
			std::set<std::string> skillParents;
			for(const auto & skill : skills.Vector())
			{
				require(skill.isStruct(), "Basic starting Skill row object");
				require(skill["skill"].isString(), "Basic starting Skill identifier");
				require(integer(skill["rank"], 1, 1) && parents.contains(skill["skill"].String())
					&& skillParents.insert(skill["skill"].String()).second,
					"Basic starting perk belongs to each starting Skill");
			}
			require(id >= 0 && static_cast<size_t>(id) < LIBRARY->heroh->objects.size()
				&& LIBRARY->heroh->objects[id] && LIBRARY->heroh->objects[id]->heroClass,
				"installed starting hero class");
			const auto faction = LIBRARY->heroh->objects[id]->heroClass->faction;
			const auto & own = starts["factionSkills"][FactionID::encode(faction.getNum())];
			require(own.isString() && skillParents.contains(own.String()), "own Basic starting faction Skill");
		}
	}
}

void initializeStartingHeroPerks(PerkState & state, const JsonNode & development, HeroTypeID hero,
	const std::vector<PerkSelection> & profile, const std::vector<PerkSelection> & prototype,
	const std::function<int(const std::string &)> & rank)
{
	nhLoadProfile::Scope measured(nhLoadProfile::Metric::StartingPerks);
	if(!usesAuthoredStartingPerks(development))
	{
		selectStartingHeroPerks(state, profile, prototype, rank);
		return;
	}
	validateStartingPerkProfiles(development["startingSkills"]);
	require(usesUnifiedSkillProgression(state.rules), "starting defaults require unified Skill progression");
	auto proposed = state;
	// End every borrowed rules view before moving the completed candidate.
	[&]
	{
		PerkSelectionOperation operation(proposed);
		for(const auto & choice : proposed.selected)
			require(rank(choice.skillId) >= selectedTier(operation.definitions(), choice), "owned authored starting perk rank");
		Preferences preferred;
		appendPreferences(preferred, operation.definitions(), prototype);
		appendPreferences(preferred, operation.definitions(), profile);
		appendPreferences(preferred, operation.definitions(), defaultsFor(development, hero));
		for(size_t count = 0; count < PERK_CANONICAL_SKILL_COUNT * PERK_MAX_PERKS_PER_SKILL; ++count)
		{
			const auto offer = operation.next(rank);
			if(offer.empty())
			{
				operation.commit(proposed);
				return;
			}
			const auto & first = offer.front();
			const auto preference = preferred.find({first.selection.skillId, first.requiredRank});
			const auto choice = preference == preferred.end() ? offer.end()
				: std::ranges::find_if(offer, [&](const auto & candidate)
					{ return candidate.selection == preference->second; });
			const auto & selected = choice == offer.end() ? first.selection : choice->selection;
			operation.select(selected.skillId, selected.perkId, rank(selected.skillId));
		}
		if(!operation.next(rank).empty())
			throw std::runtime_error("Starting perk completion exceeded the captured registry");
		operation.commit(proposed);
	}();
	state = std::move(proposed);
}
}
