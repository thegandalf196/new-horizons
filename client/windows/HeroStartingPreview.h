/*
 * HeroStartingPreview.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../../lib/entities/hero/HeroStartingProjection.h"
#include "HeroSpecialtyPresentation.h"
#include "../../lib/texts/ITranslator.h"
#include <map>
#include <stdexcept>
class CMapInfo;
class CGHeroInstance;
struct HeroStartingBiography
{
	std::string textID;
	std::string translated;
};

// Copy text while the parsed map/captured context still exists. This reads no
// gameplay state beyond the matching hero's existing biography identity.
// The caller must supply its actual translation context; no global fallback.
inline HeroStartingBiography heroStartingBiography(const CHero & prototype, const ITranslator & translator,
	const CGHeroInstance * hero = nullptr)
{
	if(hero && hero->getHeroTypeID() != prototype.getId())
		throw std::invalid_argument("Starting biography requires the matching hero identity");
	const auto textID = hero ? hero->getBiographyTextID() : prototype.getBiographyTextID();
	return {textID, translator.translate(textID)};
}

struct HeroStartingPreview
{
	newHorizonsHeroes::StartingHeroProjection values;
	HeroSpecialtyPresentation specialty;
	HeroStartingBiography biography;
	std::vector<int> stackChances;
	std::map<std::string, std::string> perkNames;
	std::map<SecondarySkill, std::string> skillDescriptions;
	std::map<SecondarySkill, std::string> skillNames;
	bool defaultProfile = true;
};
HeroStartingPreview heroStartingPreview(HeroTypeID hero, const CMapInfo * selectedMap = nullptr,
	PlayerColor owner = PlayerColor::NEUTRAL, const CGHeroInstance * capturedHero = nullptr);
std::string startingSkillPerkNames(const HeroStartingPreview & preview, SecondarySkill skill);
std::string startingSkillName(const HeroStartingPreview & preview, SecondarySkill skill);
std::string startingSkillDescription(const HeroStartingPreview & preview, SecondarySkill skill, int rank);

