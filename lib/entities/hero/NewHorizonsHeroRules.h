/*
 * NewHorizonsHeroRules.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "NewHorizonsPrimaryProfile.h"
#include "NewHorizonsPrimaryGrowth.h"
#include <functional>
#include <vector>

namespace newHorizonsHeroes
{
constexpr int HERO_RULESET_VERSION = 1;

struct DLL_LINKAGE SkillGrowthChance
{
	SecondarySkill skill;
	PrimarySkill attribute;
	int chancePercent = 0;
};

/// Read-only live-hero presentation. No secondary attributes or masteries are
/// fabricated here. Guaranteed growth excludes the independent chance outcomes.
struct DLL_LINKAGE PrimaryGrowthView
{
	PrimaryProfile profile;
	std::array<int, GameConstants::PRIMARY_SKILLS> base{};
	std::array<int, GameConstants::PRIMARY_SKILLS> modified{};
	std::vector<SkillGrowthChance> extraGrowth;
	std::array<int, GameConstants::PRIMARY_SKILLS> lastGains{};
	int powerDivisor = 1;
	int maximumPrimary = 0;
};

DLL_LINKAGE bool usesRules(const JsonNode & rules);
/// New-game completeness differs from validation of an existing saved roster.
DLL_LINKAGE void validateHeroRules(const JsonNode & rules, bool requireAllClasses);
DLL_LINKAGE void validateResolvedHeroRules(const JsonNode & rules);
DLL_LINKAGE JsonNode resolveHeroRules(const JsonNode & rules, HeroClassID heroClass);
DLL_LINKAGE std::vector<SkillGrowthChance> skillGrowthChances(const JsonNode & resolvedRules,
	const std::function<int(SecondarySkill)> & rank);
}
