/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleRoundHealing.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/HealingReceipt.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
namespace battleRoundHealing
{
namespace
{
void publishRegeneration(CGameHandler & gameHandler, const CBattleInfoCallback & battle,
	const battle::Unit & unit, BattleTriggerEffect & effect)
{
	if(battle::healingReceivedReduction(unit) == 0)
	{
		gameHandler.sendAndApply(effect); // Preserve the legacy/no-dust packet and arithmetic.
		return;
	}
	auto state = unit.acquireState();
	int64_t requested = effect.val;
	const auto receipt = state->heal(requested, EHealLevel::HEAL, EHealPower::PERMANENT);
	if(receipt.healedHealthPoints <= 0)
		return; // No false regeneration animation or requested-HP log for a fully blocked receipt.
	BattleUnitsChanged changed;
	changed.battleID = battle.getBattle()->getBattleID();
	UnitChanges update(unit.unitId(), UnitChanges::EOperation::UPDATE);
	update.data = state->save();
	update.healthDelta = receipt.healedHealthPoints;
	changed.changedStacks.push_back(std::move(update));
	gameHandler.sendAndApply(changed);
	BattleAnimationPlayed animation;
	animation.battleID = changed.battleID;
	animation.animation = AnimationPath::builtin("SP12_.DEF");
	animation.sound = AudioPath::builtin("REGENER");
	animation.transparency = 0.5f;
	animation.targets.push_back({static_cast<int32_t>(unit.unitId()), unit.getPosition()});
	animation.deferred = false;
	gameHandler.sendAndApply(animation);
	BattleLogMessage message;
	message.battleID = changed.battleID;
	MetaString line;
	line.appendRawString("Regeneration restores %s ");
	line.replaceName(unit.creatureId(), unit.getCount());
	line.appendNumber(receipt.healedHealthPoints);
	line.appendRawString(" Health.");
	message.lines.push_back(std::move(line));
	gameHandler.sendAndApply(message);
}
}
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
		publishRegeneration(gameHandler, battle, *unit, effect);
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
			publishRegeneration(gameHandler, battle, *stack, bte);
	}
}
}
