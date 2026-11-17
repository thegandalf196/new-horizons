/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleEntryEffects.h"
#include "BattleInfo.h"
#include "NewHorizonsPrismaticVanguard.h"
#include "../entities/creature/NewHorizonsRecruitmentTraining.h"
void battle::initializeEntryEffects(BattleInfo & battle, int32_t day, int32_t week)
{
	battle.trainingEntrySnapshot = newHorizonsTraining::captureEntry(battle, day, week);
	newHorizonsTraining::addEntryBonuses(battle, day, week);
	newHorizonsPrismaticVanguard::captureInitialEntries(battle);
}
