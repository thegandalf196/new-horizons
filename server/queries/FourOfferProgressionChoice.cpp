/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroLevelUpOffer.h"
#include "../CGameHandler.h"
#include "../../lib/entities/hero/NewHorizonsFourOfferProgression.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include <algorithm>
#include <stdexcept>

namespace newHorizonsLevelUp
{
void applyFourOfferChoice(CGameHandler & handler, const CGHeroInstance * hero,
	const newHorizonsHeroes::FourOfferProgressionOffer & offer, int32_t reply)
{
	const auto candidates = newHorizonsHeroes::fourOfferCandidates(handler.gameInfo(), *hero);
	const auto legal = newHorizonsHeroes::legalFourOfferReplies(offer, hero->getPerkState(), candidates);
	if(legal.empty() && reply == 0) return;
	if(!vstd::contains(legal, reply)) throw std::runtime_error("Unavailable four-offer advancement");
	const auto selected = newHorizonsHeroes::fourOfferSelection(offer, hero->getPerkState(), reply);
	if(!selected) throw std::runtime_error("Invalid four-offer advancement reply");
	if(selected->exhausted) return;
	if(selected->skill != SecondarySkill::NONE)
	{
		handler.levelUpHero(hero, selected->skill, false);
		return;
	}
	for(const auto & group : newHorizonsHeroes::fourOfferPerkGroups(offer, candidates))
	{
		const auto candidate = std::find(group.begin(), group.end(), *selected->perk);
		if(candidate == group.end()) continue;
		handler.levelUpHero(hero, group, std::distance(group.begin(), candidate), 0, false);
		return;
	}
	throw std::runtime_error("Missing selected four-offer perk group");
}

void applyFirstFourOfferChoice(CGameHandler & handler, const CGHeroInstance * hero,
	const newHorizonsHeroes::FourOfferProgressionOffer & offer)
{
	const auto legal = newHorizonsHeroes::legalFourOfferReplies(offer, hero->getPerkState(),
		newHorizonsHeroes::fourOfferCandidates(handler.gameInfo(), *hero));
	const int32_t reply = legal.empty() ? 0 : legal.front();
	applyFourOfferChoice(handler, hero, offer, reply);
}
}
