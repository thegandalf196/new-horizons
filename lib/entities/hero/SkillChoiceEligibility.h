/*
 * SkillChoiceEligibility.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../../constants/EntityIdentifiers.h"
#include <set>
#include <vector>
class CGHeroInstance;
class IGameInfoCallback;
namespace newHorizonsHeroes
{
/// Read-only ordinary level-up legality, using the callback that owns the hero view.
DLL_LINKAGE bool isValidLevelUpSkillChoice(const IGameInfoCallback & info,
	const CGHeroInstance * hero, SecondarySkill skill);
/// Exact legal Connoisseur pool: excludes all offered cards; consumes no RNG.
DLL_LINKAGE std::set<SecondarySkill> connoisseurSkillCandidates(const IGameInfoCallback & info,
	const CGHeroInstance & hero, const std::vector<SecondarySkill> & offeredSkills);
}
