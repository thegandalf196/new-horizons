/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleExchangeAftermath.h"
#include "AttackPreviewHitContext.h"
#include "MeleeSpecialtyProjection.h"
#include "CoordinatedMarkProjection.h"
#include "ParalyzingGazeProjection.h"
#include "DefiantDenialProjection.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "BattleExchangeVariant.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include "../../lib/battle/NewHorizonsPlague.h"
BattleScore battleExchangeAftermath::finalizeExchangeScore(BattleScore score,
	int simulationTurnsCount, int totalTurnsCount, int turn)
{
	if(simulationTurnsCount < totalTurnsCount)
	{
		float scalingRatio = simulationTurnsCount / static_cast<float>(totalTurnsCount);

		score.enemyDamageReduce *= scalingRatio;
		score.ourDamageReduce *= scalingRatio;
	}

	if(turn > 0)
	{
		auto turnMultiplier = 1 - std::min(0.2, 0.05 * turn);

		score.enemyDamageReduce *= turnMultiplier;
	}

#if BATTLE_TRACE_LEVEL>=1
	logAi->trace("Exchange score: enemy: %2f, our -%2f", score.enemyDamageReduce, score.ourDamageReduce);
#endif

	return score;
}



void BattleExchangeVariant::completePlagueActivation(uint32_t unitId, PlayerColor referenceController,
	bool referenceActorIsOurs, DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & hb)
{
	const auto firstHit = hb->getProjectedPlagueHits().size();

	const auto actor = hb->getForUpdate(unitId);
	newHorizonsPlague::completeActivation(*hb, *hb->getServerCallback(), unitId);
	const auto & hits = hb->getProjectedPlagueHits();
	for(size_t index = firstHit; index < hits.size(); ++index)
	{
		const auto & hit = hits[index];
		const auto value = AttackPossibility::calculateDamageReduce(nullptr, hit.preHitTarget.get(),
			static_cast<uint64_t>(hit.finalizedDamage), damageCache, hb);
		const bool targetIsOurs = (hit.targetController == referenceController) == referenceActorIsOurs;
		if(targetIsOurs)
			dpsScore.ourDamageReduce += value;
		else
			dpsScore.enemyDamageReduce += value;
	}

}

void BattleExchangeVariant::completeFrozenActivation(const std::shared_ptr<StackWithBonuses> & attacker, bool referenceActorIsOurs,
	DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & exchangeBattle)
{
	const auto firstHit = exchangeBattle->getProjectedPlagueHits().size();
	const auto controller = exchangeBattle->battleGetOwner(attacker.get());
	exchangeBattle->completeForfeitedActivation(attacker->unitId());
	const auto & hits = exchangeBattle->getProjectedPlagueHits();
	for(size_t index = firstHit; index < hits.size(); ++index)
	{
		const auto & hit = hits[index];
		const auto value = AttackPossibility::calculateDamageReduce(nullptr, hit.preHitTarget.get(),
			static_cast<uint64_t>(hit.finalizedDamage), damageCache, exchangeBattle);
		const bool ours = (hit.targetController == controller) == referenceActorIsOurs;
		if(ours) dpsScore.ourDamageReduce += value;
		else dpsScore.enemyDamageReduce += value;
	}
}

namespace battleExchangeAftermath
{
bool projectNoQuarterAfterHit(HypotheticBattle & battle, const BattleAttackInfo & attack,
	StackWithBonuses & target)
{
	if(!battle.battleCanTriggerNoQuarter(attack) || !target.alive() || target.isTimeStopped()
		|| !battle.battleMatchOwner(attack.attacker, &target)
		|| !newHorizonsOffense::belowNoQuarterThreshold(
			target.getAvailableHealth(), battle::getMaximumHealth(target)))
		return false;
	if(battleDefiantAI::project(battle, attack, newHorizonsArmorer::DefiantDenialCause::NO_QUARTER))
		return false;

	const int32_t moraleActivations = battle.getBattle()->getActiveStackID()
		== static_cast<int32_t>(target.unitId()) ? 2 : 1;
	target.applyNoQuarter(moraleActivations, true);
	return true;
}

std::optional<uint32_t> relentlessAssaultPrimaryTargetId(const CBattleInfoCallback & battle,
	const BattleAttackInfo & attack)
{
	const auto * attacker = attack.attacker;
	const auto * defender = attack.defender;
	if(!attacker || !defender || !attack.physicalDamage || attack.retaliation
		|| attack.secondaryAttack || attack.bracePreemptive || attack.preemptiveDamagePercent > 0
		|| attack.cleaveDamagePercent > 0 || attacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK))
		return {};
	const auto side = attacker->unitSide();
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return {};
	const auto * hero = battle.battleGetFightingHero(side);
	if(!hero || !hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::RELENTLESS_ASSAULT)
		|| !attacker->alive() || attacker->isGhost() || attacker->isTurret()
		|| attacker->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| attacker->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER)
		return {};
	const auto * primaryTarget = battle.battleResolveHeroOrderTarget(attacker, defender, attack.shooting);
	if(!primaryTarget || !primaryTarget->alive() || primaryTarget->isGhost() || primaryTarget->isTurret()
		|| primaryTarget->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| primaryTarget->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
		|| !battle.battleMatchOwner(attacker, primaryTarget))
		return {};
	return primaryTarget->unitId();
}

void projectRelentlessAssaultAttack(HypotheticBattle & battle, const BattleAttackInfo & attack,
	std::optional<uint32_t> primaryTargetUnitId)
{
	if(!attack.attacker || !primaryTargetUnitId)
		return;
	battle.recordRelentlessAssaultAttack(attack.attacker->unitSide(), *primaryTargetUnitId);
}
}

void battleExchangeAftermath::replayPrimaryStatuses(HypotheticBattle & battle,
	const BattleAttackInfo & attack, const FortuneStrikeProjection & strike,
	const std::vector<std::pair<uint32_t, int64_t>> & actualHits,
	bool binding, bool gaze, int64_t primaryBodyHealthLoss)
{
	const auto primaryHit = std::ranges::find(actualHits, strike.defenderId,
		&std::pair<uint32_t, int64_t>::first);
	applyPrimaryStatuses(battle, attack,
		primaryHit == actualHits.end() ? 0 : primaryHit->second, binding, gaze, primaryBodyHealthLoss,
		primaryHit == actualHits.end() ? newHorizonsOrdinaryStrike::EffectPlan{} : strike.lanceFormation);
}

void battleExchangeAftermath::applyPrimaryStatuses(HypotheticBattle & battle,
	const BattleAttackInfo & attack, int64_t primaryDamage, bool binding, bool gaze, int64_t primaryBodyHealthLoss,
	const newHorizonsOrdinaryStrike::EffectPlan & lanceFormation)
{
	battle.projectSerpentBinding(attack.attacker->unitId(), attack.defender->unitId(), primaryDamage, binding);
	paralyzingGazeAI::apply(battle, attack.attacker->unitId(), attack.defender->unitId(), primaryDamage, gaze);
	AttackPreviewHitContext::applyOrdinaryStrike(battle, attack, primaryDamage, primaryBodyHealthLoss);
	AttackPreviewHitContext::applyLanceFormation(battle, lanceFormation);
}

void BattleExchangeVariant::completeSelectedAttack(const AttackPossibility & ap,
	const std::shared_ptr<HypotheticBattle> & hb, StackWithBonuses & attacker, BattleHex originalPosition, bool returnCommitted,
	const std::optional<HypotheticBattle::ManaLeechReplay> & manaLeechReplay,
	PlayerColor referenceController, DamageCache & damageCache)
{
	bool returned = returnCommitted;
	if(!ap.coordinatedReturnHandled && !ap.attack.shooting && ap.from.isValid() && attacker.alive()
		&& attacker.hasBonusOfType(BonusType::RETURN_AFTER_STRIKE)
		&& !attacker.hasBonusOfType(BonusType::NOT_ACTIVE)
		&& !attacker.hasBonusOfType(BonusType::BIND_EFFECT))
	{
		attacker.setPosition(originalPosition);
		returned = true;
	}

#if BATTLE_TRACE_LEVEL >= 1
	logAi->trace(
		"ap score: our: %2f, enemy: %2f, collateral: %2f, blocked: %2f",
		ap.attackerDamageReduce,
		ap.defenderDamageReduce,
		ap.collateralDamageReduce,
		ap.shootersBlockedDmg);
#endif
	if(manaLeechReplay)
		hb->applyManaLeechReplay(*manaLeechReplay);

	meleeSpecialtyAI::complete(*hb, attacker.unitId(), meleeSpecialtyAI::completedPrimaryMelee(ap));
	if(returned)
		coordinatedMarkAI::replayReturn(ap, *hb, originalPosition);
}
