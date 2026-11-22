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

void validateBasicSkillProfiles(const JsonNode & starts, bool singlePerk)
{
	if(!singlePerk)
		require(!starts.Struct().contains("startingPerkProfiles"), "skills-only starts cannot contain a perk table");
	const auto & profiles = starts["startingDevelopmentProfiles"];
	require(profiles.isStruct() && starts["factionSkills"].isStruct(), "skills-only development and faction maps");
	for(const auto & hero : LIBRARY->heroh->objects)
		if(hero && HeroTypeID::encode(hero->getId().getNum()).starts_with("core:"))
			require(profiles.Struct().contains(HeroTypeID::encode(hero->getId().getNum())),
				"complete registered Core starting Skills");
	for(const auto & [identity, profile] : profiles.Struct())
	{
		const int id = resolve(HeroTypeID::entityType(), identity);
		require(HeroTypeID::encode(id) == identity && id >= 0
			&& static_cast<size_t>(id) < LIBRARY->heroh->objects.size()
			&& LIBRARY->heroh->objects[id] && LIBRARY->heroh->objects[id]->heroClass,
			"canonical installed skills-only hero");
		fields(profile, {"skills"});
		const auto & skills = profile["skills"];
		require(skills.isVector() && (skills.Vector().size() == 2 || (singlePerk && usesThreeSkillStart(starts, identity))),
			"exactly two skills-only starting parents");
		std::set<std::string> parents;
		for(const auto & skill : skills.Vector())
		{
			fields(skill, {"skill", "rank"});
			require(skill["skill"].isString() && integer(skill["rank"], 1, 1), "Basic skills-only starting row");
			const auto & key = skill["skill"].String();
			const auto installed = resolve(SecondarySkill::entityType(), key);
			require(SecondarySkill::encode(installed) == key && parents.insert(key).second,
				"distinct canonical skills-only starting parents");
		}
		const auto faction = LIBRARY->heroh->objects[id]->heroClass->faction;
		const auto & own = starts["factionSkills"][FactionID::encode(faction.getNum())];
		require(own.isString() && parents.contains(own.String()), "own Basic skills-only starting faction Skill");
	}
}

void validateSinglePerkProfiles(const JsonNode & starts)
{
	validateBasicSkillProfiles(starts, true);
	const auto & profiles = starts["startingDevelopmentProfiles"].Struct();
	const auto & choices = starts["startingPerkProfiles"];
	require(choices.isStruct() && choices.Struct().size() == profiles.size(), "complete single starting perk table");
	for(const auto & [hero, profile] : profiles)
	{
		const auto found = choices.Struct().find(hero);
		require(found != choices.Struct().end() && found->second.isVector()
			&& (found->second.Vector().size() == 1 || usesThreeSkillStart(starts, hero)),
			"exactly one authored starting perk per hero");
		if(found->second.Vector().empty())
			continue;
		const auto & choice = found->second.Vector().front();
		fields(choice, {"skill", "perk"});
		require(choice["skill"].isString() && choice["perk"].isString(), "single starting perk identifiers");
		const auto & parent = choice["skill"].String();
		require(choice["perk"].String().starts_with(parent + ".")
			&& std::ranges::any_of(profile["skills"].Vector(), [&](const auto & skill)
				{ return skill["skill"].String() == parent; }), "single starting perk belongs to a starting Skill");
	}
}

void validateHistoricalStartingPerkProfiles(const JsonNode & starts)
{
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

void initializeSingleStartingPerk(PerkState & state, const JsonNode & development, HeroTypeID hero,
	const std::function<int(const std::string &)> & rank)
{
	// Existing authored or earned choices are never completed or replaced, and an
	// explicitly empty three-Skill profile grants no perk.
	if(!state.getSelected().empty()
		|| (hero != HeroTypeID::NONE && usesThreeSkillStart(development["startingSkills"], HeroTypeID::encode(hero.getNum()))))
		return;
	auto proposed = state;
	{
		PerkSelectionOperation operation(proposed);
		const auto preferred = defaultsFor(development, hero);
		if(!preferred.empty())
		{
			const auto & choice = preferred.front();
			require(selectedTier(operation.definitions(), choice) == 1, "single starting perk must be Basic");
			if(operation.canAcquire(choice.skillId, choice.perkId, rank(choice.skillId)))
			{
				operation.select(choice.skillId, choice.perkId, rank(choice.skillId));
			}
		}
		if(operation.selections().empty())
		{
			// Explicit scenario Skill rosters can omit the default perk's parent.
			// Resolve one legal Basic choice deterministically; never invent a Skill.
			const auto offer = operation.next(rank);
			if(!offer.empty())
			{
				const auto & choice = offer.front();
				require(choice.requiredRank == 1, "fresh fallback starting perk must be Basic");
				operation.select(choice.selection.skillId, choice.selection.perkId, rank(choice.selection.skillId));
			}
		}
		operation.commit(proposed);
	}
	state = std::move(proposed);
}

}

bool usesAuthoredStartingPerks(const JsonNode & development)
{
	const auto & starts = development["startingSkills"];
	return usesRules(development) && starts["startingPerksPolicyVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& (integer(starts["startingPerksPolicyVersion"], 1, 2) || usesSingleStartingPerkProfiles(starts));
}

bool usesBasicStartingProfiles(const JsonNode & starts)
{
	return starts["startingPerksPolicyVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& integer(starts["startingPerksPolicyVersion"], 2, 4);
}

bool usesSkillsOnlyStartingProfiles(const JsonNode & starts)
{
	return starts["startingPerksPolicyVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& starts["startingPerksPolicyVersion"].Integer() == 3;
}

bool usesSingleStartingPerkProfiles(const JsonNode & starts)
{
	return starts["startingPerksPolicyVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& starts["startingPerksPolicyVersion"].Integer() == 4;
}

bool usesThreeSkillStart(const JsonNode & starts, const std::string & hero)
{
	if(!usesSingleStartingPerkProfiles(starts))
		return false;
	const auto & perks = starts["startingPerkProfiles"][hero];
	const auto & skills = starts["startingDevelopmentProfiles"][hero]["skills"];
	return perks.isVector() && perks.Vector().empty() && skills.isVector() && skills.Vector().size() == 3;
}

void validateStartingPerkProfiles(const JsonNode & starts)
{
	nhLoadProfile::Scope measured(nhLoadProfile::Metric::ProfileValidation);
	require(starts.isStruct(), "starting Skill policy object");
	if(usesSkillsOnlyStartingProfiles(starts))
	{
		validateBasicSkillProfiles(starts, false);
		return;
	}
	if(usesSingleStartingPerkProfiles(starts))
	{
		validateSinglePerkProfiles(starts);
		return;
	}
	validateHistoricalStartingPerkProfiles(starts);
}

void initializeStartingHeroPerks(PerkState & state, const JsonNode & development, HeroTypeID hero,
	const std::vector<PerkSelection> & profile, const std::vector<PerkSelection> & prototype,
	const std::function<int(const std::string &)> & rank)
{
	nhLoadProfile::Scope measured(nhLoadProfile::Metric::StartingPerks);
	if(usesSkillsOnlyStartingProfiles(development["startingSkills"]))
	{
		validateStartingPerkProfiles(development["startingSkills"]);
		return; // Creation grants no perks and never rewrites an existing selection.
	}
	if(!usesAuthoredStartingPerks(development))
	{
		selectStartingHeroPerks(state, profile, prototype, rank);
		return;
	}
	validateStartingPerkProfiles(development["startingSkills"]);
	require(usesPersistentHeroProgression(state.getRules()), "starting defaults require captured Skill progression");
	if(usesSingleStartingPerkProfiles(development["startingSkills"]))
	{
		initializeSingleStartingPerk(state, development, hero, rank);
		return;
	}
	auto proposed = state;
	// End every borrowed rules view before moving the completed candidate.
	[&]
	{
		PerkSelectionOperation operation(proposed);
		for(const auto & choice : proposed.getSelected())
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
