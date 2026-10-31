/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattlePrimaryStrikeAftermath.h"
#include "../../lib/battle/NewHorizonsCreatureBurn.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/NewHorizonsReactiveDefense.h"
#include "../../lib/battle/NewHorizonsChargeSpecialties.h"
#include "../../lib/battle/NewHorizonsCoordinatedMarks.h"
#include "../../lib/battle/NewHorizonsGraveDustStrike.h"
#include "../../lib/battle/BodyHealthLoss.h"
#include "NewHorizonsParalyzingGaze.h"
#include "../CGameHandler.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include <algorithm>
#include <stdexcept>
namespace
{
newHorizonsOrdinaryStrike::EffectPlan prepareGraveDust(const CBattleInfoCallback & battle,
	const BattleAttackInfo & attack, const std::vector<BattleStackAttacked> & hits)
{
	if(!attack.shooting || !attack.attacker || !attack.defender) return {};
	const auto primary = std::ranges::find_if(hits, [&attack](const BattleStackAttacked & hit)
	{
		return hit.stackAttacked == attack.defender->unitId() && !hit.isSecondary();
	});
	if(primary == hits.end() || primary->killed() || primary->cloneKilled() || primary->willRebirth()) return {};
	// This owner is called for the actual makeAttack primary. Counterfire/Overwatch
	// mask older riders as secondary, but retain their own non-secondary primary BSA.
	auto context = attack;
	context.secondaryAttack = false; // Dust-only context; the original input remains unchanged.
	if(!newHorizonsGraveDustStrike::eligible(battle, context)) return {};
	const auto * before = dynamic_cast<const battle::CUnitState *>(attack.defender);
	if(!before) throw std::runtime_error("Grave Dust requires a concrete prehit body snapshot");
	const auto bodyLoss = battle::bodyHealthLoss(primary->damageAmount, before->health.getTemporaryHitPoints());
	return newHorizonsGraveDustStrike::plan(battle, context, bodyLoss, false);
}
}

namespace battlePrimaryStrikeAftermath
{
Prepared prepare(const CBattleInfoCallback & battle, const BattleAttackInfo & attack,
	const std::vector<BattleStackAttacked> & hits)
{
	Prepared result;
	if(!attack.attacker || !attack.defender)
		return result;
	result.toxicSting = battleToxicSting::prepare(battle, attack, hits);
	result.source = attack.attacker->unitId();
	result.target = attack.defender->unitId();
	result.hostilePrimaryMelee = attack.physicalDamage && !attack.shooting && !attack.retaliation
		&& !attack.secondaryAttack
		&& battle.battleGetOwner(attack.attacker) != battle.battleGetOwner(attack.defender);
	const auto primary = std::ranges::find(hits, result.target, &BattleStackAttacked::stackAttacked);
	const int64_t damage = primary == hits.end() ? 0 : primary->damageAmount;
	result.gaze = newHorizonsParalyzingGaze::qualifiesAttack(battle, attack.attacker, attack.defender,
		attack.shooting, attack.physicalDamage, attack.secondaryAttack) && damage > 0;
	result.binding = newHorizonsSerpentsEmbrace::prepareBinding(battle, attack.attacker, attack.defender,
		attack.shooting, attack.physicalDamage, attack.secondaryAttack, hits);
	int64_t bodyLoss = 0;
	if(newHorizonsCreatureBurn::eligible(battle, attack))
	{
		const auto * before = dynamic_cast<const battle::CUnitState *>(attack.defender);
		if(!before)
			throw std::runtime_error("Creature burn requires a concrete prehit health snapshot");
		bodyLoss = newHorizonsCreatureBurn::bodyHealthLoss(damage, before->health.getTemporaryHitPoints());
	}
	result.ordinary = newHorizonsOrdinaryStrike::plan(battle, attack, damage, bodyLoss);
	if(primary != hits.end())
		result.lanceFormation = newHorizonsChargeSpecialties::lanceFormation(battle, attack);
	result.graveDust = prepareGraveDust(battle, attack, hits);
	return result;
}
void recordPrimaryMelee(const Prepared & prepared, std::optional<uint32_t> * victim)
{
	if(prepared.hostilePrimaryMelee && victim)
		*victim = prepared.target;
}
void apply(CGameHandler & game, const CBattleInfoCallback & battle, const Prepared & prepared)
{
	newHorizonsSerpentsEmbrace::applyBinding(game, battle, prepared.binding);
	if(prepared.gaze)
		newHorizonsParalyzingGaze::applyAfterShot(game, battle, prepared.source, prepared.target, true);
	applyOrdinaryEffect(game, battle, prepared.ordinary);
	applyOrdinaryEffect(game, battle, prepared.lanceFormation);
	if(!prepared.graveDust.add.empty()) applyOrdinaryEffect(game, battle, prepared.graveDust);
}
void applyOrdinaryEffect(CGameHandler & game, const CBattleInfoCallback & battle,
	const newHorizonsOrdinaryStrike::EffectPlan & effectPlan)
{
	const auto * recipient = battle.battleGetUnitByID(effectPlan.target);
	if(!recipient || (effectPlan.add.empty() && effectPlan.remove.empty()))
		return;
	const bool available = recipient->alive() && !recipient->isGhost() && !recipient->isTimeStopped();
	SetStackEffect effect;
	effect.battleID = battle.getBattle()->getBattleID();
	std::vector<Bonus> remove;
	for(const auto & bonus : effectPlan.remove)
		if(available || newHorizonsCoordinatedMarks::isMark(&bonus)
			|| (!recipient->isTimeStopped() && newHorizonsReactiveDefense::isIronHideSpent(&bonus)))
			remove.push_back(bonus);
	if(!remove.empty())
		effect.toRemove.emplace_back(effectPlan.target, std::move(remove));
	std::vector<Bonus> add;
	for(const auto & bonus : effectPlan.add)
		if(available || (!recipient->isTimeStopped() && newHorizonsReactiveDefense::isIronHideSpent(&bonus)))
			add.push_back(bonus);
	if(!add.empty())
		effect.toAdd.emplace_back(effectPlan.target, std::move(add));
	if(!effect.toRemove.empty() || !effect.toAdd.empty())
		game.sendAndApply(effect);
}

}
