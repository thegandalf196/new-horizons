/*
 * HeroLevelUpOffer.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#pragma once

class CGameHandler;
class CGHeroInstance;
struct HeroLevelUp;

namespace newHorizonsLevelUp
{
void prepareChoices(CGameHandler & handler, const CGHeroInstance * hero, HeroLevelUp & offer);
void validateNoPendingOptionalOffer(const CGameHandler & handler);
}
