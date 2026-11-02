/*
 * NewHorizonsLevelUpOffer.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#pragma once

class CGHeroInstance;
struct HeroLevelUp;
namespace newHorizonsHeroes
{
DLL_LINKAGE void applyLevelUpOffer(CGHeroInstance * hero, const HeroLevelUp & pack);
}
