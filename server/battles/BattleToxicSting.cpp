/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleToxicSting.h"
#include "BattleProcessor.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/BodyHealthLoss.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/mapObjects/army/CArmedInstance.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
namespace battleToxicSting
{
std::optional<newHorizonsToxicSting::Plan> prepare(const CBattleInfoCallback & battle,
	const BattleAttackInfo & attack, const std::vector<BattleStackAttacked> & hits)
{
	if(!attack.defender)
		return std::nullopt;
	const auto hit = std::ranges::find(hits, attack.defender->unitId(), &BattleStackAttacked::stackAttacked);
	const auto * before = dynamic_cast<const battle::CUnitState *>(attack.defender);
	if(hit == hits.end() || !before)
		return std::nullopt;
	return newHorizonsToxicSting::prepare(battle, attack,
		battle::bodyHealthLoss(hit->damageAmount, before->health.getTemporaryHitPoints()));
}
void apply(CGameHandler & game, BattleProcessor & processor, const CBattleInfoCallback & battle,
	const std::optional<newHorizonsToxicSting::Plan> & prepared)
{
	if(!prepared)
		return;
	const auto * source = battle.battleGetUnitByID(prepared->source);
	const auto * target = battle.battleGetUnitByID(prepared->target);
	if(!source || !source->alive() || !target || !newHorizonsToxicSting::livingTarget(*target)
		|| battle.battleGetOwner(source) == battle.battleGetOwner(target))
		return;
	auto state = target->acquireState();
	if(!state || prepared->baseDamage < state->physicalPoisonBaseDamage)
		return;
	const auto affected = battle.playerToSide(battle.battleGetOwner(target));
	const auto * army = battle.getBattle()->getSideArmy(source->unitSide());
	if(!army || (affected != BattleSide::ATTACKER && affected != BattleSide::DEFENDER))
		return;
	const int chance = game.randomizer->prepareFavorableCreatureAbilityChance(*source, prepared->chancePercent);
	const auto draw = [&game, army, chance]() { return game.randomizer->rollCombatAbility(army->id, chance); };
	if(!processor.resolveAdverseCombatRoll(battle.getBattle()->getBattleID(), affected,
		chance > 0 && chance < 100, true, draw) || !newHorizonsToxicSting::apply(*prepared, *state))
		return;
	BattleUnitsChanged changed;
	changed.battleID = battle.getBattle()->getBattleID();
	UnitChanges update(state->unitId(), UnitChanges::EOperation::UPDATE);
	update.data = state->save();
	changed.changedStacks.push_back(std::move(update));
	game.sendAndApply(changed);
	BattleLogMessage message;
	message.battleID = changed.battleID;
	MetaString line;
	line.appendRawString("Toxic Sting poisons %s for three activations, starting at ");
	target->addNameReplacement(line, target->getCount());
	line.appendNumber(prepared->baseDamage);
	line.appendRawString(" physical damage.");
	message.lines.push_back(std::move(line));
	game.sendAndApply(message);
}
}
