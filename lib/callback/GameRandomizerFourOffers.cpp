/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "GameRandomizer.h"
#include "../entities/hero/NewHorizonsFourOfferProgression.h"
#include "../mapObjects/CGHeroInstance.h"

newHorizonsHeroes::FourOfferProgressionOffer IGameRandomizer::rollFourOfferProgression(const CGHeroInstance * hero)
{
	if(!hero || !hero->cb) throw std::runtime_error("Four-offer generation requires a hero callback");
	const auto candidates = newHorizonsHeroes::fourOfferCandidates(*hero->cb, *hero);
	auto result = newHorizonsHeroes::prepareFourOfferProgression(candidates,
		[this](const auto & weights) { return RandomGeneratorUtil::nextItemWeighted(weights, getDefault()); });
	newHorizonsHeroes::validateFourOfferProgression(result, candidates);
	return result;
}

newHorizonsHeroes::FourOfferProgressionOffer GameRandomizer::rollFourOfferProgression(const CGHeroInstance * hero)
{
	if(!hero) throw std::runtime_error("Four-offer generation requires a hero");
	if(!heroSkillSeed.count(hero->getHeroTypeID()))
		heroSkillSeed.try_emplace(hero->getHeroTypeID(), getDefault().nextInt());
	auto & rng = heroSkillSeed.at(hero->getHeroTypeID()).seed;
	const auto candidates = newHorizonsHeroes::fourOfferCandidates(gameInfo, *hero);
	auto result = newHorizonsHeroes::prepareFourOfferProgression(candidates,
		[&rng](const auto & weights) { return RandomGeneratorUtil::nextItemWeighted(weights, rng); });
	newHorizonsHeroes::validateFourOfferProgression(result, candidates);
	return result;
}
