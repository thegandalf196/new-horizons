/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsHeroRules.h"
#include "../../constants/Enumerations.h"

namespace newHorizonsHeroes
{
std::optional<SecondarySkill> factionSkill(const JsonNode & resolvedRules, FactionID faction)
{
	if(!usesRules(resolvedRules) || !resolvedRules["startingSkills"].isStruct())
		return std::nullopt;

	const auto & factionSkills = resolvedRules["startingSkills"]["factionSkills"];
	if(!factionSkills.isStruct())
		return std::nullopt;

	const auto skillName = factionSkills[FactionID::encode(faction.getNum())].String();
	if(skillName.empty())
		return std::nullopt;

	const auto skillId = SecondarySkill::decode(skillName);
	if(skillId < 0)
		return std::nullopt;
	return SecondarySkill(skillId);
}

namespace
{
void appendMergedSkill(std::vector<std::pair<SecondarySkill, ui8>> & result,
	SecondarySkill skill, ui8 rank)
{
	const auto existing = std::find_if(result.begin(), result.end(),
		[skill](const auto & value) { return value.first == skill; });
	if(existing == result.end())
		result.emplace_back(skill, rank);
	else
		existing->second = std::max(existing->second, rank);
}

std::vector<std::pair<SecondarySkill, ui8>> mergeStartingSkills(
	const std::vector<std::pair<SecondarySkill, ui8>> & skills)
{
	std::vector<std::pair<SecondarySkill, ui8>> result;
	result.reserve(skills.size());
	for(const auto & [skill, rank] : skills)
		appendMergedSkill(result, skill, rank);
	return result;
}

const JsonNode * findStartingSkillMigration(const JsonNode & resolvedRules, SecondarySkill skill)
{
	if(!usesRules(resolvedRules) || !resolvedRules["startingSkills"].isStruct())
		return nullptr;
	const auto & migrations = resolvedRules["startingSkills"]["legacySkillMigrations"];
	if(!migrations.isStruct())
		return nullptr;
	const auto it = migrations.Struct().find(SecondarySkill::encode(skill.getNum()));
	return it == migrations.Struct().end() ? nullptr : &it->second;
}

SecondarySkill migrationTarget(const JsonNode & migration)
{
	const auto target = migration["target"].String();
	const auto dot = target.find('.');
	const auto skillName = target.substr(0, dot);
	const auto id = SecondarySkill::decode(skillName);
	if(id < 0)
		throw std::runtime_error("Invalid New Horizons starting-skill migration target " + target);
	return SecondarySkill(id);
}
}

std::optional<SecondarySkill> normalizeRewardSkill(const JsonNode & resolvedRules, SecondarySkill skill)
{
	if(skill == SecondarySkill::NONE || skill.getNum() < 0)
		return std::nullopt;

	if(!usesRules(resolvedRules))
		return skill;

	if(const auto * migration = findStartingSkillMigration(resolvedRules, skill))
	{
		const auto kind = (*migration)["kind"].String();
		if(kind == "skill")
			skill = migrationTarget(*migration);
		else
			// Faction skills need a hero-faction context and perk migrations are
			// not secondary skills. Reward loading has no such context, so fail
			// closed instead of leaking a retired core identity.
			return std::nullopt;
	}

	if(isExcludedSkill(resolvedRules, skill))
		return std::nullopt;

	return skill;
}

std::vector<std::pair<SecondarySkill, ui8>> migrateStartingSkills(
	const JsonNode & resolvedRules, FactionID faction,
	const std::vector<std::pair<SecondarySkill, ui8>> & initialSkills)
{
	if(!usesRules(resolvedRules) || !resolvedRules["startingSkills"].isStruct())
		return mergeStartingSkills(initialSkills);

	std::vector<std::pair<SecondarySkill, ui8>> result;
	result.reserve(initialSkills.size());
	for(const auto & [skill, rank] : initialSkills)
	{
		const auto * migration = findStartingSkillMigration(resolvedRules, skill);
		if(!migration)
		{
			appendMergedSkill(result, skill, rank);
			continue;
		}

		const auto kind = (*migration)["kind"].String();
		if(kind == "perk")
		{
			// Perk-at-start is not implemented. The authored target and explicit
			// remove fallback keep this honest: do not grant its parent Skill as a
			// proxy and do not retain a retired level-up identity.
			continue;
		}

		const auto target = migrationTarget(*migration);
		if(kind == "factionSkill")
		{
			// A faction target is meaningful only for the hero's own faction. A
			// malformed/custom cross-faction roster remains readable instead of
			// silently granting a foreign faction Skill.
			const auto ownFactionSkill = factionSkill(resolvedRules, faction);
			if(!ownFactionSkill || *ownFactionSkill != target)
			{
				appendMergedSkill(result, skill, rank);
				continue;
			}
		}
		appendMergedSkill(result, target, rank);
	}
	return result;
}

namespace
{
std::optional<SecondarySkill> legacyFactionSkillAlias(const JsonNode & resolvedRules, FactionID faction)
{
	if(!usesRules(resolvedRules) || !resolvedRules["startingSkills"].isStruct())
		return std::nullopt;

	const auto & aliases = resolvedRules["startingSkills"]["legacyAliases"];
	if(!aliases.isStruct())
		return std::nullopt;

	const auto aliasName = aliases[FactionID::encode(faction.getNum())].String();
	if(aliasName.empty())
		return std::nullopt;

	const auto aliasId = SecondarySkill::decode(aliasName);
	if(aliasId < 0)
		return std::nullopt;
	return SecondarySkill(aliasId);
}
}

bool isFactionSkillForFaction(const JsonNode & resolvedRules, FactionID faction, SecondarySkill skill)
{
	if(const auto canonical = factionSkill(resolvedRules, faction); canonical && *canonical == skill)
		return true;
	if(const auto alias = legacyFactionSkillAlias(resolvedRules, faction); alias && *alias == skill)
		return true;
	return false;
}

bool isFactionSkill(const JsonNode & resolvedRules, SecondarySkill skill)
{
	if(!usesRules(resolvedRules) || !resolvedRules["startingSkills"].isStruct())
		return false;

	const auto & startingSkills = resolvedRules["startingSkills"];
	const auto & factionSkills = startingSkills["factionSkills"];
	if(factionSkills.isStruct())
		for(const auto & [faction, ignored] : factionSkills.Struct())
			if(isFactionSkillForFaction(resolvedRules,
				FactionID(FactionID::decode(faction)), skill))
				return true;
	return false;
}

std::vector<std::pair<SecondarySkill, ui8>> applyStartingFactionSkill(
	const JsonNode & resolvedRules, bool magicHero, FactionID faction,
	const std::vector<std::pair<SecondarySkill, ui8>> & initialSkills)
{
	std::vector<std::pair<SecondarySkill, ui8>> result = mergeStartingSkills(initialSkills);
	if(!usesRules(resolvedRules) || !resolvedRules["startingSkills"].isStruct())
		return result;

	const auto factionSkillId = factionSkill(resolvedRules, faction);
	if(!factionSkillId)
		return result;
	const auto & startingSkills = resolvedRules["startingSkills"];
	const SecondarySkill factionSkill = *factionSkillId;
	const SecondarySkill wisdomReplacement(SecondarySkill::decode(
		startingSkills["magic"]["replace"].String()));
	const SecondarySkill legacyWisdom = SecondarySkill::WISDOM;

	// Necromancy was already present in the legacy roster. Convert that legacy
	// identity to the New Horizons faction Skill instead of leaving two parallel
	// skills with the same player-facing name. If an authored roster already has
	// both identities, keep the first one's position and the strongest mastery.
	const auto legacyAliasId = legacyFactionSkillAlias(resolvedRules, faction);
	const SecondarySkill legacyAlias = legacyAliasId.value_or(SecondarySkill::NONE);
	std::vector<std::pair<SecondarySkill, ui8>> normalized;
	normalized.reserve(result.size());
	for(const auto & [skill, rank] : result)
	{
		if(skill == factionSkill || (legacyAlias != SecondarySkill::NONE && skill == legacyAlias))
		{
			appendMergedSkill(normalized, factionSkill, rank);
		}
		else
			appendMergedSkill(normalized, skill, rank);
	}
	result = std::move(normalized);

	if(magicHero)
	{
		const auto wisdomIt = std::find_if(result.begin(), result.end(),
			[wisdomReplacement, legacyWisdom](const auto & value)
			{
				// Accept the legacy identity as well as the canonical target. This
				// keeps direct callers and old resolved snapshots readable while
				// creation migration uses the scoped New Horizons identity.
				return value.first == wisdomReplacement || value.first == legacyWisdom;
			});
		const auto factionIt = std::find_if(result.begin(), result.end(),
			[factionSkill](const auto & value) { return value.first == factionSkill; });
		if(wisdomIt != result.end())
		{
			const auto wisdomIndex = static_cast<size_t>(std::distance(result.begin(), wisdomIt));
			const auto wisdomRank = wisdomIt->second;
			if(factionIt == result.end())
			{
				// Wisdom is replaced in-place so authored skill ordering remains
				// stable for both magic hero defaults and explicit map rosters.
				result[wisdomIndex] = {factionSkill,
					static_cast<ui8>(wisdomRank > 0 ? wisdomRank : static_cast<ui8>(MasteryLevel::BASIC))};
			}
			else
			{
				const auto factionIndex = static_cast<size_t>(std::distance(result.begin(), factionIt));
				result[factionIndex].second = std::max(result[factionIndex].second, wisdomRank);
				if(factionIndex > wisdomIndex)
				{
					result[wisdomIndex] = result[factionIndex];
					result.erase(result.begin() + static_cast<std::ptrdiff_t>(factionIndex));
				}
				else
				{
					result.erase(result.begin() + static_cast<std::ptrdiff_t>(wisdomIndex));
				}
			}
		}
		else if(factionIt == result.end())
			result.emplace_back(factionSkill, MasteryLevel::BASIC);
		return result;
	}

	// Wisdom is a Magic-only New Horizons Skill. A legacy Might hero can still
	// carry core:wisdom in its authored starting roster (Rashka is the shipped
	// example), but it must not leak that identity into a fresh hero. Spellcraft
	// is the generic, non-retired replacement for a Might hero's old magical
	// training; the faction Skill then occupies the authored second position.
	const SecondarySkill spellcraft(SecondarySkill::decode("new-horizons:spellcraft"));
	std::vector<std::pair<SecondarySkill, ui8>> mightNormalized;
	mightNormalized.reserve(result.size());
	for(const auto & [skill, rank] : result)
	{
		if(skill == wisdomReplacement || skill == legacyWisdom)
			appendMergedSkill(mightNormalized, spellcraft, rank);
		else
			appendMergedSkill(mightNormalized, skill, rank);
	}
	result = std::move(mightNormalized);

	if(std::any_of(result.begin(), result.end(),
		[factionSkill](const auto & value) { return value.first == factionSkill; }))
		return result;

	const auto & might = startingSkills["might"];
	const ui8 replacementRank = result.size() > 1 ? result[1].second : static_cast<ui8>(MasteryLevel::BASIC);
	if(might["replacePosition"].String() == "second" && result.size() > 1)
		result[1] = {factionSkill, replacementRank};
	else if(result.size() == 1 && might["singleSkillFallback"].String() == "replaceFirst")
		result[0] = {factionSkill, result[0].second};
	else
		result.emplace_back(factionSkill, replacementRank);
	return result;
}


}
