/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleRangedFollowup.h"
#include "../CGameHandler.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/NewHorizonsMachineSpecialties.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
namespace battleRangedFollowup
{
void publish(const CBattleInfoCallback & battle, CGameHandler & gameHandler,
	const battle::Unit * unit, int percent)
{
	if(!unit)
		return;
	auto state = unit->acquireState();
	if(!state || state->rangedFollowUpDamagePercent == percent)
		return;
	state->setRangedFollowUpDamagePercent(percent);

	BattleUnitsChanged update;
	update.battleID = battle.getBattle()->getBattleID();
	UnitChanges change(unit->unitId(), UnitChanges::EOperation::UPDATE);
	change.data = state->save();
	update.changedStacks.push_back(std::move(change));
	gameHandler.sendAndApply(update);
}

void log(const CBattleInfoCallback & battle, CGameHandler & gameHandler,
	const battle::Unit * unit, std::string text)
{
	if(!unit)
		return;
	BattleLogMessage message;
	message.battleID = battle.getBattle()->getBattleID();
	MetaString line;
	line.appendRawString(text);
	unit->addNameReplacement(line, unit->getCount());
	message.lines.push_back(std::move(line));
	gameHandler.sendAndApply(message);
}

void grant(const CBattleInfoCallback & battle, CGameHandler & handler, const battle::Unit * unit,
	const CGHeroInstance * hero, int ordinaryPercent)
{
	const int percent = newHorizonsMachineSpecialties::masterGunnerFollowUpDamagePercent(hero, ordinaryPercent);
	publish(battle, handler, unit, percent);
	log(battle, handler, unit, "The Master Gunner grants %s one " + std::to_string(percent) + "% follow-up shot.");
}
}
