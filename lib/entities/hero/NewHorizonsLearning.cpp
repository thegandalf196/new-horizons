/*
 * NewHorizonsLearning.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsLearning.h"
#include "../../mapObjects/CGHeroInstance.h"
namespace newHorizonsLearning
{
int combatExperienceAdditionalPercent(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk("new-horizons:learning", "new-horizons:learning.fieldStudy") ? 25 : 0;
}
int64_t duelLessonsExperience(const CGHeroInstance * winner, int64_t defeatedEnemyExperience)
{
	if(!winner || defeatedEnemyExperience <= 0
		|| !winner->hasActivePerk("new-horizons:learning", "new-horizons:learning.duelLessons"))
		return 0;
	return defeatedEnemyExperience / 10;
}
int64_t combatExperience(const CGHeroInstance * hero, int64_t rawExperience, int64_t defeatedEnemyExperience)
{
	if(!hero)
		return rawExperience;
	const auto ordinary = hero->calculateXp(rawExperience, combatExperienceAdditionalPercent(hero));
	const auto duel = duelLessonsExperience(hero, defeatedEnemyExperience);
	return ordinary > std::numeric_limits<int64_t>::max() - duel
		? std::numeric_limits<int64_t>::max() : ordinary + duel;
}
int trainingPrimaryReward(const CGHeroInstance * hero, std::optional<Obj> source, int amount)
{
	if(!hero || !source || amount <= 0
		|| !hero->hasActivePerk("new-horizons:learning", "new-horizons:learning.seasonedAdventurer"))
		return amount;
	switch(*source)
	{
	case Obj::MERCENARY_CAMP:
	case Obj::MARLETTO_TOWER:
	case Obj::STAR_AXIS:
	case Obj::GARDEN_OF_REVELATION:
	case Obj::LIBRARY_OF_ENLIGHTENMENT:
	case Obj::SCHOOL_OF_MAGIC:
	case Obj::SCHOOL_OF_WAR:
	case Obj::ARENA:
		return static_cast<int>(std::min<int64_t>(int64_t(amount) * 125 / 100, std::numeric_limits<int>::max()));
	default:
		return amount;
	}
}
}
