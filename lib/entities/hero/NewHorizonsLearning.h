/*
 * NewHorizonsLearning.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../../constants/EntityIdentifiers.h"
#include <optional>
class CGHeroInstance;
namespace newHorizonsLearning
{
DLL_LINKAGE int combatExperienceAdditionalPercent(const CGHeroInstance * hero);
/// Raw extra XP: 10% of the defeated enemy hero total XP captured before removal.
DLL_LINKAGE int64_t duelLessonsExperience(const CGHeroInstance * winner, int64_t defeatedEnemyExperience);
DLL_LINKAGE int64_t combatExperience(const CGHeroInstance * hero, int64_t rawExperience, int64_t defeatedEnemyExperience = 0);
/// Positive permanent primary rewards from training locations only, floored once.
DLL_LINKAGE int trainingPrimaryReward(const CGHeroInstance * hero, std::optional<Obj> source, int amount);
}
