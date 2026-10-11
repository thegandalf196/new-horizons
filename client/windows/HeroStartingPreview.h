/*
 * HeroStartingPreview.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../../lib/entities/hero/HeroStartingProjection.h"
#include "HeroSpecialtyPresentation.h"
#include <map>
class CMapInfo;
class CGHeroInstance;
struct HeroStartingPreview
{
	newHorizonsHeroes::StartingHeroProjection values;
	HeroSpecialtyPresentation specialty;
	std::vector<int> stackChances;
	std::map<std::string, std::string> perkNames;
	bool defaultProfile = true;
};
HeroStartingPreview heroStartingPreview(HeroTypeID hero, const CMapInfo * selectedMap = nullptr,
	PlayerColor owner = PlayerColor::NEUTRAL, const CGHeroInstance * capturedHero = nullptr);
std::string startingSkillPerkNames(const HeroStartingPreview & preview, SecondarySkill skill);

