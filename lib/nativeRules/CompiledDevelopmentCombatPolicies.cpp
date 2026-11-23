/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CompiledDevelopmentRules.h"
#include "../entities/hero/NewHorizonsParalyzingGaze.h"
#include "../entities/hero/NewHorizonsSerpentsEmbrace.h"
#include "../entities/hero/NewHorizonsForestAmbush.h"
#include "../entities/hero/NewHorizonsAerialSupremacy.h"
#include "../entities/hero/NewHorizonsDreadCavalry.h"
#include "../entities/hero/NewHorizonsManaLeeches.h"
#include "../entities/hero/NewHorizonsGhostSpecialty.h"
#include "../entities/hero/NewHorizonsGenieSage.h"
#include "../entities/hero/NewHorizonsChosenSpecialties.h"
namespace nativeRules
{
void CompiledDevelopmentRules::compileCombatPolicies(const JsonNode & rules)
{
	paralyzingGaze = newHorizonsHeroes::paralyzingGazePolicy(rules);
	serpentsEmbrace = newHorizonsHeroes::serpentsEmbracePolicy(rules);
	forestAmbush = newHorizonsHeroes::forestAmbushPolicy(rules);
	aerialSupremacy = newHorizonsHeroes::aerialSupremacyPolicy(rules);
	dreadCavalry = newHorizonsHeroes::dreadCavalryDamagePercent(rules);
	levelScaling = newHorizonsHeroes::specialtyLevelScaling(rules);
	castleSpecialties = newHorizonsHeroes::castleSpecialtyRules(rules);
	castleAdventure = newHorizonsHeroes::castleAdventureSpecialties(rules);
	rampartSpecialties = newHorizonsHeroes::rampartSpecialtyRules(rules);
	manaLeeches = newHorizonsHeroes::manaLeechesAmount(rules);
	livingInferno = newHorizonsHeroes::livingInfernoRules(rules);
	herbalSurgeon = newHorizonsHeroes::herbalSurgeonRules(rules);
	swampSeer = newHorizonsHeroes::swampSeerRules(rules);
	reclamation = newHorizonsHeroes::reclamationSpecialtyRules(rules);
	for(size_t i = 0; i < learningExchange.size(); ++i)
		learningExchange[i] = newHorizonsHeroes::learningExchangeSpecialtyRules(rules,
			newHorizonsHeroes::LearningExchangeSpecialtyKind(i));
	ghostUpgrade = newHorizonsHeroes::usesGhostUpgrade(rules);
	genieSage = newHorizonsHeroes::usesGenieSage(rules);
	petrifyingBlood = rules.Struct().contains("petrifyingBloodSpecialty");
	const SpellID chosen(SpellID::decode("new-horizons:theChosen"));
	if(chosen.hasValue())
		for(const auto * key : {"core:inteus", "core:loynis"})
		{
			const HeroTypeID hero(HeroTypeID::decode(key));
			if(!hero.hasValue()) continue;
			chosenDuration.emplace(std::pair(hero, chosen), newHorizonsHeroes::chosenSpecialtyDurationBonus(rules, key, chosen));
			chosenDiscount.emplace(std::pair(hero, chosen), newHorizonsHeroes::chosenSpecialtyManaDiscount(rules, key, chosen));
		}
}
int CompiledDevelopmentRules::chosenDurationBonus(HeroTypeID hero, SpellID spell) const
{
	const auto found = chosenDuration.find({hero, spell});
	return found == chosenDuration.end() ? 0 : found->second;
}
int CompiledDevelopmentRules::chosenManaDiscount(HeroTypeID hero, SpellID spell) const
{
	const auto found = chosenDiscount.find({hero, spell});
	return found == chosenDiscount.end() ? 0 : found->second;
}
}
