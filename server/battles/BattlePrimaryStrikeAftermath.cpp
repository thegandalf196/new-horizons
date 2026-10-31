/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattlePrimaryStrikeAftermath.h"
#include "../../lib/battle/NewHorizonsChargeSpecialties.h"
#include "../../lib/battle/NewHorizonsCoordinatedMarks.h"
#include "NewHorizonsParalyzingGaze.h"
#include "../CGameHandler.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/SetStackEffect.h"
namespace battlePrimaryStrikeAftermath
{
Prepared prepare(const CBattleInfoCallback & battle, const BattleAttackInfo & attack,
	const std::vector<BattleStackAttacked> & hits)
{
	Prepared result;
	if(!attack.attacker || !attack.defender)
		return result;
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
	result.ordinary = newHorizonsOrdinaryStrike::plan(battle, attack, damage);
	if(primary != hits.end())
		result.lanceFormation = newHorizonsChargeSpecialties::lanceFormation(battle, attack);
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
		if(available || newHorizonsCoordinatedMarks::isMark(&bonus))
			remove.push_back(bonus);
	if(!remove.empty())
		effect.toRemove.emplace_back(effectPlan.target, std::move(remove));
	if(available && !effectPlan.add.empty())
		effect.toAdd.emplace_back(effectPlan.target, effectPlan.add);
	if(!effect.toRemove.empty() || !effect.toAdd.empty())
		game.sendAndApply(effect);
}

}
