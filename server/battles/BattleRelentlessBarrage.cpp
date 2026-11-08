/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleActionProcessor.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/NewHorizonsAttackControllers.h"

void BattleActionProcessor::performRelentlessBarrage(const CBattleInfoCallback & battle, const CStack * source,
	const CStack * target, BattleHex impact, int shots, RelentlessAssaultActionContext & relentless, RainOfArrowsAction & rain)
{
	if(!source || !target || shots <= 0
		|| !newHorizonsAttackControllers::barrageRepeatAllowed(battle, source, target, target->unitId(), impact)) return;
	const int percent = newHorizonsAttackControllers::barrageDamagePercent(battle, *source);
	if(percent == 0) return;
	const auto battleId = battle.getBattle()->getBattleID();
	const auto * nativeOwner = gameHandler->gameState().getBattle(battleId);
	const auto sourceId = source->unitId();
	const auto targetId = target->unitId();
	static const auto firstStrikeSelector = Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeAll)
		.Or(Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeRanged));
	const bool defenderFirst = target->hasBonus(firstStrikeSelector) && !target->hasBonusOfType(BonusType::NOT_ACTIVE);
	if(defenderFirst) performOrdinaryRangedRetaliation(battle, target, source);
	for(int i = 0; i < shots; ++i)
	{
		if(gameHandler->gameState().getBattle(battleId) != nativeOwner) return;
		// Exact same recipient, no retargeting or ammo restoration. Each repeat
		// uses the native projectile/shot admission, including intervening deaths.
		source = battle.battleGetStackByID(sourceId, false);
		target = battle.battleGetStackByID(targetId, false);
		if(!newHorizonsAttackControllers::barrageRepeatAllowed(battle, source, target, targetId, impact)) break;
		makeAttack(battle, source, target, {.targetHex = impact, .attackIndex = i, .first = i == 0,
			.ranged = true, .archeryRangedDamageMultiplierPercent = percent}, nullptr, &relentless, &rain);
		const auto * current = gameHandler->gameState().getBattle(battleId);
		if(!current || current != nativeOwner) return;
		source = current->battleGetStackByID(sourceId, false);
		target = current->battleGetStackByID(targetId, false);
		if(!source || !target) return;
		if(i == 0 && !defenderFirst) performOrdinaryRangedRetaliation(battle, target, source);
	}
}
