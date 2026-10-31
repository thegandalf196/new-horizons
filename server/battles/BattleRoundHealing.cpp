/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleRoundHealing.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
namespace battleRoundHealing
{
void afterRound(CGameHandler & gameHandler, const CBattleInfoCallback & battle)
{
	for(const auto * unit : battle.battleGetAllUnits(false))
	{
		const auto healing = newHorizonsHeroes::ordinaryCreatureRoundHealing(battle, *unit);
		if(healing <= 0)
			continue;
		BattleTriggerEffect effect;
		effect.battleID = battle.getBattle()->getBattleID();
		effect.stackID = unit->unitId();
		effect.effect = BonusType::HP_REGENERATION;
		effect.val = healing;
		gameHandler.sendAndApply(effect);
	}
}

void beforeTurn(CGameHandler & gameHandler, const CBattleInfoCallback & battle, const CStack * stack)
{
	// regeneration takes place before everything else but only during first turn attempt in each round
	// also works under blind and similar effects
	if(stack && stack->alive() && !stack->waiting && !stack->isTimeStopped())
	{
		BattleTriggerEffect bte;
		bte.battleID = battle.getBattle()->getBattleID();
		bte.stackID = stack->unitId();
		bte.effect = BonusType::HP_REGENERATION;

		const int32_t lostHealth = stack->getMaxHealth() - stack->getFirstHPleft();
		if(stack->hasBonusOfType(BonusType::HP_REGENERATION))
			bte.val = std::min(lostHealth, stack->valOfBonuses(BonusType::HP_REGENERATION));

		if(bte.val) // anything to heal
			gameHandler.sendAndApply(bte);
	}
}
}
