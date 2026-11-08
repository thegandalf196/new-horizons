/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BarrageValue.h"
#include "AttackPossibility.h"
#include "../../lib/battle/NewHorizonsAttackControllers.h"
#include <algorithm>
namespace barrageAI
{
std::optional<Prepared> prepare(const AttackPossibility & action, int shots,
	const std::shared_ptr<CBattleInfoCallback> & board)
{
	const auto & attack = action.attack;
	if(!attack.shooting || attack.retaliation || attack.secondaryAttack || attack.bracePreemptive
		|| attack.preemptiveDamagePercent > 0 || attack.cleaveDamagePercent > 0 || !attack.attacker || !attack.defender) return {};
	const auto percent = newHorizonsAttackControllers::barrageDamagePercent(*board, *attack.attacker);
	if(percent <= 0) return {};
	const auto * target = dynamic_cast<const battle::CUnitState *>(attack.defender);
	const auto * source = action.attackerState.get();
	if(!target || !source || target->isClone() || target->getPhantomInitialIntegrity() > 0
		|| target->health.isCapacityHealthTracking() || shots <= 0) return {};
	shots = std::min(shots, source->shots.available());
	if(shots <= 0) return {};
	auto nominal = attack;
	nominal.attacker = source;
	nominal.attackerPos = source->getPosition();
	nominal.defenderPos = action.dest;
	if(action.perfectMoment || board->battleCanTriggerPerfectFortune(nominal)) nominal.luckyStrike = true;
	// Luck outcomes and reaction-dependent ranges are not replaced by their
	// mean. The bounded tail models only a fixed admitted native damage range.
	if(!nominal.luckyStrike && !nominal.unluckyStrike
		&& board->battleGetAttackLuck(source, target, true) != 0) return {};
	const auto damage = board->calculateDmgRange(nominal).damage;
	const bool guardian = battleAIDamageProvenance(source, attack.physicalDamage) == battle::DamageProvenance::PHYSICAL_CREATURE
		&& target->getGuardianSpiritRoundsRemaining() > 0;
	const barrageProbability::TargetWindow window{target->getFirstHPleft(), target->getAvailableHealth(),
		target->health.getTemporaryHitPoints(), target->arcaneShieldHitPoints,
		guardian ? target->getGuardianSpiritHitPoints() : 0};
	return Prepared{barrageProbability::primaryCasualtyAndSurvival(damage, source->getCount(), shots, window), shots, percent};
}
float value(const AttackPossibility & action, const Prepared & prepared, DamageCache & cache,
	const std::shared_ptr<CBattleInfoCallback> & board)
{
	if(prepared.probability <= 0) return 0;
	const auto * source = action.attackerState.get();
	const auto targetEntry = std::ranges::find_if(action.affectedUnits, [&](const auto & unit)
		{ return unit->unitId() == action.attack.defender->unitId(); });
	if(targetEntry == action.affectedUnits.end()) return 0;
	const auto * target = targetEntry->get();
	if(!newHorizonsAttackControllers::barrageRepeatAllowed(*board, source, target,
		action.attack.defender->unitId(), action.dest)) return 0;
	const auto shots = std::min(prepared.shots, source->shots.available());
	auto victim = target->acquireState();
	float result = 0;
	for(int index = 0; index < shots && victim->alive(); ++index)
	{
		auto repeat = action.attack;
		repeat.attacker = source; repeat.defender = victim.get();
		repeat.archeryRangedDamageMultiplierPercent = prepared.damagePercent;
		repeat.luckyStrike = false; repeat.unluckyStrike = false;
		const auto raw = board->battleExpectedLuckDamage(repeat);
		const auto loss = battleAIProjectDamage(victim.get(), raw, battleAIDamageProvenance(source, repeat.physicalDamage));
		result += AttackPossibility::calculateDamageReduce(source, victim.get(), loss.healthLoss, cache, board);
		auto amount = raw;
		victim->damage(amount, false, battleAIDamageProvenance(source, repeat.physicalDamage));
	}
	// Existing attack-pressure units; the static repeat does not forecast new
	// reactions, Luck history or future ammo. Those omissions stay explicit.
	return static_cast<float>(result * prepared.probability);
}
}
