/*
 * NewHorizonsMysticismRecovery.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsRecoverySpecialties.h"
#include <algorithm>
#include "../../mapObjects/CGHeroInstance.h"
namespace newHorizonsHeroes
{
int64_t originalMysticismRecovery(const CGHeroInstance & hero)
{
	return hero.hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.mysticism")
		? std::max<int64_t>(5, static_cast<int64_t>(hero.manaLimit()) / 10) : 0;
}
int64_t enhancedMysticismRecovery(const CGHeroInstance & hero, bool completedDay)
{
	const auto original = originalMysticismRecovery(hero);
	if(original == 0)
		return 0;
	const auto rules = recoverySpecialtyRules(hero);
	if(!rules)
		return original;
	const auto percent = std::min<int64_t>(static_cast<int64_t>(hero.level) * rules->dailyPercentPerLevel,
		rules->dailyMaximumPercent);
	const auto enhanced = original + original * percent / 100;
	const bool lowNormal = completedDay
		&& static_cast<int64_t>(hero.getNormalSpellPoints()) * 2 < hero.manaLimit();
	return enhanced + (lowNormal ? original * rules->lowNormalPercent / 100 : 0);
}
int64_t firstCombatMysticismRecovery(const CGHeroInstance & hero)
{
	if(!hasCapturedDeepTrance(hero))
		return 0;
	const auto rules = recoverySpecialtyRules(hero);
	const auto requested = originalMysticismRecovery(hero) * rules->firstCombatPercent / 100;
	const auto missing = std::max<int64_t>(0, static_cast<int64_t>(hero.manaLimit()) - hero.getNormalSpellPoints());
	return std::min(requested, missing);
}
int64_t victoryMysticismRecovery(const CGHeroInstance & hero)
{
	if(!hero.getHeroTypeID().hasValue()
		|| hero.getHeroTypeID() != HeroTypeID(HeroTypeID::decode("core:axsis")))
		return 0;
	const auto rules = recoverySpecialtyRules(hero);
	return rules ? enhancedMysticismRecovery(hero, false) * rules->victoryPercent / 100 : 0;
}
}
