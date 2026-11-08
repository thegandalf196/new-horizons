/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattlePointBlankPresentation.h"
#include "../GameEngine.h"
#include "../gui/CursorHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/NewHorizonsAttackControllers.h"

namespace pointBlankPresentation
{
bool setCursor(const CBattleInfoCallback & battle, const CStack * shooter, const BattleHex & target)
{
	if(!shooter || shooter->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK)
		|| newHorizonsAttackControllers::ballistaPhysicalDamagePercent(battle, *shooter) != 200)
		return false;
	ENGINE->cursor().set(battle.battleHasShootingPenalty(shooter, target)
		? "combatPointBlankShootPenalty" : "combatPointBlankShoot");
	return true;
}

std::string status(const CBattleInfoCallback & battle, const CStack & shooter, const BattleHex & target)
{
	if(shooter.hasBonusOfType(BonusType::SPELL_LIKE_ATTACK)
		|| newHorizonsAttackControllers::ballistaPhysicalDamagePercent(battle, shooter) != 200)
		return {};
	std::string result = "\nPoint-Blank Shot: x2 physical damage (included above); ordinary shot and ammunition count.";
	const bool distance = battle.battleHasDistancePenalty(&shooter, shooter.getPosition(), target);
	const bool wall = battle.battleHasWallPenalty(&shooter, shooter.getPosition(), target);
	result += distance ? " Reduced damage: distance penalty." : " Full range: no distance penalty.";
	result += wall ? " Wall penalty applies." : " No wall penalty.";
	return result;
}
}
