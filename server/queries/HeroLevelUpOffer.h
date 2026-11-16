/*
 * HeroLevelUpOffer.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#pragma once
#include "../../lib/constants/EntityIdentifiers.h"

class CGameHandler;
class CGHeroInstance;
struct HeroLevelUp;
namespace newHorizonsHeroes { struct FourOfferProgressionOffer; }

namespace newHorizonsLevelUp
{
void prepareChoices(CGameHandler & handler, const CGHeroInstance * hero, HeroLevelUp & offer);
HeroLevelUp prepareLevelUp(CGameHandler & handler, const CGHeroInstance * hero);
bool admitExternalSkillRank(const CGHeroInstance & hero, SecondarySkill skill, int targetRank);
void completeSkillRank(CGameHandler & handler, const CGHeroInstance * hero, SecondarySkill skill);
void validateNoPendingOptionalOffer(const CGameHandler & handler);
void applyFourOfferChoice(CGameHandler & handler, const CGHeroInstance * hero,
	const newHorizonsHeroes::FourOfferProgressionOffer & offer, int32_t reply);
void applyFirstFourOfferChoice(CGameHandler & handler, const CGHeroInstance * hero,
	const newHorizonsHeroes::FourOfferProgressionOffer & offer);
}
