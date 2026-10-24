/*
 * Part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsReclaimer.h"
#include "NewHorizonsHeroRules.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
namespace newHorizonsHeroes
{
bool resolveReclaimerPresentation(const CHero & hero, const JsonNode & development,
	const CGHeroInstance * capturedHero, SpecialtyPresentation & result)
{
	if(hero.getJsonKey() != "core:vidomina")
		return false;
	const auto rule = reclamationSpecialtyRules(development);
	if(!rule)
		return false;
	result.name = "Dark Reclaimer";
	result.skill = SecondarySkill(SecondarySkill::decode("new-horizons:necromancy"));
	result.description = "After ordinary Necromancy conversions, " + std::to_string(rule->conversionPercentPerLevel)
		+ "% per hero level of remaining Skeleton equivalents, up to " + std::to_string(rule->conversionMaximumPercent)
		+ "%, may become Zombies: three equivalents per Zombie. Fractions remain Skeletons. "
		"Requires an owned Necropolis with a built Walking Dead or Zombie dwelling; stock is not spent. "
		"It grants no perk or extra eligible corpses. A complete ordinary raise is retained if the enhanced batch cannot fit. "
		"Black Harvest counts the final creatures delivered.";
	if(capturedHero)
		result.description += " Current conversion share before the owned-dwelling requirement: "
			+ std::to_string(cappedSpecialtyContribution(capturedHero->level, rule->conversionPercentPerLevel,
				rule->conversionMaximumPercent)) + "%.";
	return true;
}
}
