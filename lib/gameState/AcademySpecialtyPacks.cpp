/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "GameStatePackVisitor.h"
#include "CGameState.h"
#include "../entities/hero/NewHorizonsAcademySpecialties.h"
#include "../mapObjects/CGHeroInstance.h"

void GameStatePackVisitor::visitSetNewHorizonsSpellPractice(SetNewHorizonsSpellPractice & pack)
{
	auto * hero = gs.getHero(pack.heroID);
	if(!hero)
		throw std::runtime_error("Practice Makes Perfect references a missing hero");
	newHorizonsHeroes::setSpellPractice(*hero, pack.spell, pack.percent);
}
