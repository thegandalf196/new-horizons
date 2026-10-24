/*
 * NewHorizonsWellspringRecovery.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsWellspringRecovery.h"
#include "../CGameHandler.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/entities/hero/NewHorizonsRecoverySpecialties.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
void applyWellspringVictoryRecovery(CGameHandler & handler, BattleID battleID, const CGHeroInstance * winner)
{
	if(!winner)
		return;
	const auto requested = newHorizonsHeroes::victoryMysticismRecovery(*winner);
	const auto missing = std::max<int64_t>(0, static_cast<int64_t>(winner->manaLimit()) - winner->getNormalSpellPoints());
	const auto amount = static_cast<int32_t>(std::min(requested, missing));
	if(amount == 0)
		return;
	const auto before = winner->getNormalSpellPoints();
	handler.restoreSpellPoints(winner->id, amount);
	const auto actual = winner->getNormalSpellPoints() - before;
	if(actual <= 0)
		return;
	BattleLogMessage log;
	log.battleID = battleID;
	MetaString line = MetaString::createFromTextID(winner->getNameTextID());
	line.appendRawString(": Infernal Wellspring restores ");
	line.appendNumber(actual);
	line.appendRawString(" Normal Spell Points after victory.");
	log.lines.push_back(std::move(line));
	handler.sendAndApply(log);
}
