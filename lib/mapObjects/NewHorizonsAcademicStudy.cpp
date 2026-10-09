/*
 * NewHorizonsAcademicStudy.cpp, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsAcademicStudy.h"
#include "CGHeroInstance.h"
#include "CGTownInstance.h"
#include "../spells/NewHorizonsMagic.h"

namespace newHorizonsLearning
{
TExpType academicStudyRawExperience(const CGHeroInstance & hero, const CGTownInstance & town)
{
	if(!newHorizonsMagic::rulesActive(hero.getMagicRules())
		|| !hero.hasActivePerk("new-horizons:learning", "new-horizons:learning.academicStudy")
		|| hero.visitedObjects.contains(town.id))
		return 0;
	constexpr TExpType experiencePerGuildLevel = 250;
	return experiencePerGuildLevel * town.mageGuildLevel();
}

TExpType academicStudyExperience(const CGHeroInstance & hero, const CGTownInstance & town)
{
	const auto raw = academicStudyRawExperience(hero, town);
	return raw > 0 ? hero.calculateXp(raw) : 0;
}
}
