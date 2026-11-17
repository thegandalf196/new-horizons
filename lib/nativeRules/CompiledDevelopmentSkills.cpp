/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CompiledDevelopmentRules.h"
#include "../GameLibrary.h"
#include "../CSkillHandler.h"
namespace nativeRules
{
void CompiledDevelopmentRules::compileSkillPolicies(const JsonNode & rules)
{
	for(const auto & [key, row] : rules["startingSkills"]["factionSkills"].Struct())
	{
		const FactionID faction(FactionID::decode(key));
		if(const auto canonical = newHorizonsHeroes::factionSkill(rules, faction)) factions.emplace(faction, *canonical);
		for(int id = 0; id < LIBRARY->skillh->size(); ++id)
		{
			const SecondarySkill skill(id);
			if(newHorizonsHeroes::isFactionSkillForFaction(rules, faction, skill))
			{
				factionAliases[faction].insert(skill);
				uniqueSkills.insert(skill);
			}
		}
	}
	for(int id = 0; id < LIBRARY->skillh->size(); ++id)
	{
		const SecondarySkill skill(id);
		rewardSkills.emplace(skill, newHorizonsHeroes::normalizeRewardSkill(rules, skill));
	}
}
std::optional<SecondarySkill> CompiledDevelopmentRules::factionSkill(FactionID faction) const
{
	const auto found = factions.find(faction);
	return found == factions.end() ? std::nullopt : std::optional(found->second);
}
bool CompiledDevelopmentRules::isFactionSkillForFaction(FactionID faction, SecondarySkill skill) const
{
	const auto found = factionAliases.find(faction);
	return found != factionAliases.end() && found->second.contains(skill);
}
std::optional<SecondarySkill> CompiledDevelopmentRules::normalizeRewardSkill(SecondarySkill skill) const
{
	if(!skill.hasValue()) return std::nullopt;
	if(!enabled) return skill;
	const auto found = rewardSkills.find(skill);
	// External invalid IDs followed the old exclusion path unchanged.
	return found == rewardSkills.end() ? std::optional(skill) : found->second;
}
}
