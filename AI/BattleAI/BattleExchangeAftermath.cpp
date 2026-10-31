/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleExchangeAftermath.h"
#include "AttackPreviewHitContext.h"
#include "MeleeSpecialtyProjection.h"
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
void BattleExchangeVariant::accountForNewElementalRebirthSpawns(
	const std::set<uint32_t> & idsBefore, const battle::Unit * referenceActor,
	const bool referenceActorIsOurs, DamageCache & damageCache,
	const std::shared_ptr<HypotheticBattle> & hb)
{
	if(!referenceActor)
		return;

	for(const auto unitId : hb->getElementalRebirthSpawnUnitIds())
	{
		if(idsBefore.contains(unitId) || !scoredElementalRebirthSpawnUnitIds.insert(unitId).second)
			continue;

		const auto * spawned = hb->battleGetUnitByID(unitId);
		if(!spawned || !spawned->alive() || !spawned->unitType() || spawned->getAvailableHealth() <= 0)
			continue;

		const auto enemies = hb->battleGetUnitsIf([&](const battle::Unit * candidate)
		{
			return candidate && candidate->alive() && candidate->unitType()
				&& candidate->getPosition().isValid()
				&& !hb->battleMatchOwner(spawned, candidate);
		});
		if(enemies.empty())
			continue;

		const auto fullHealth = static_cast<int64_t>(spawned->getCount()) * spawned->getMaxHealth();
		if(fullHealth <= 0)
			continue;

		// Keep this in the exchange's damage-reduction/DPS units: scale one ordinary
		// projected attack by the exact surviving HP fraction. This is a bounded
		// representative-target approximation, not a full initiative-branch forecast.
		const auto projectedDamage = std::max<int64_t>(0,
			damageCache.getOriginalDamage(spawned, enemies.front(), hb));
		const float spawnValue = static_cast<float>(projectedDamage)
			* static_cast<float>(spawned->getAvailableHealth()) / static_cast<float>(fullHealth);
		const bool sameSideAsReference = spawned->unitSide() == referenceActor->unitSide();
		const bool spawnIsOurs = referenceActorIsOurs ? sameSideAsReference : !sameSideAsReference;
		if(spawnIsOurs)
			dpsScore.enemyDamageReduce += spawnValue;
		else
			dpsScore.ourDamageReduce += spawnValue;
	}
}

void BattleExchangeVariant::accountForNewPrimalBurstHits(const size_t firstNewHit,
	const PlayerColor referenceController, const bool referenceActorIsOurs,
	DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & hb)
{
	const auto & hits = hb->getProjectedPrimalBurstHits();
	for(size_t index = firstNewHit; index < hits.size(); ++index)
	{
		const auto & hit = hits[index];
		if(!hit.preHitTarget || hit.actualDamage <= 0)
			continue;
		const auto value = AttackPossibility::calculateDamageReduce(nullptr, hit.preHitTarget.get(),
			static_cast<uint64_t>(hit.actualDamage), damageCache, hb);
		const bool targetSharesReferenceController = hit.targetController == referenceController;
		const bool targetIsOurs = targetSharesReferenceController == referenceActorIsOurs;
		if(targetIsOurs)
			dpsScore.ourDamageReduce += value;
		else
			dpsScore.enemyDamageReduce += value;
	}
}

void BattleExchangeVariant::completePlagueActivation(uint32_t unitId, PlayerColor referenceController,
	bool referenceActorIsOurs, DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & hb)
{
	const auto firstHit = hb->getProjectedPlagueHits().size();
	const auto rebirthBefore = hb->getElementalRebirthSpawnUnitIds();
	const auto firstPrimalBurstHit = hb->getProjectedPrimalBurstHits().size();
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
	accountForNewElementalRebirthSpawns(rebirthBefore, actor.get(), referenceActorIsOurs, damageCache, hb);
	accountForNewPrimalBurstHits(firstPrimalBurstHit, referenceController, referenceActorIsOurs, damageCache, hb);
}

void BattleExchangeVariant::completeFrozenActivation(const std::shared_ptr<StackWithBonuses> & attacker, bool referenceActorIsOurs,
	DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & exchangeBattle)
{
	const auto frozen = newHorizonsFrozen::removalPlan(*attacker);
	attacker->removeUnitBonus(CSelector([](const Bonus * bonus)
	{
		return Bonus::UntilActivationEnds(bonus) && !newHorizonsOffense::isNoQuarterMoralePenalty(bonus);
	}));
	attacker->consumeNoQuarterActivation();
	completePlagueActivation(attacker->unitId(), exchangeBattle->battleGetOwner(attacker.get()),
		referenceActorIsOurs, damageCache, exchangeBattle);
	const auto current = exchangeBattle->getForUpdate(attacker->unitId());
	if(current->alive() && !current->isGhost() && !current->isTimeStopped()
		&& newHorizonsFrozen::isFrozen(*current))
		current->removeUnitBonus(frozen);
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

void battleExchangeAftermath::applyPrimaryStatuses(HypotheticBattle & battle,
	const BattleAttackInfo & attack, int64_t primaryDamage, bool binding, bool gaze)
{
	battle.projectSerpentBinding(attack.attacker->unitId(), attack.defender->unitId(), primaryDamage, binding);
	paralyzingGazeAI::apply(battle, attack.attacker->unitId(), attack.defender->unitId(), primaryDamage, gaze);
	AttackPreviewHitContext::applyOrdinaryStrike(battle, attack, primaryDamage);
}

void BattleExchangeVariant::completeSelectedAttack(const AttackPossibility & ap,
	const std::shared_ptr<HypotheticBattle> & hb, StackWithBonuses & attacker, BattleHex originalPosition,
	const std::optional<HypotheticBattle::ManaLeechReplay> & manaLeechReplay,
	const std::set<uint32_t> & rebirthSpawnIdsBefore, size_t primalBurstFirstNewHit,
	PlayerColor referenceController, DamageCache & damageCache)
{
	if(!ap.attack.shooting && ap.from.isValid() && attacker.alive()
		&& attacker.hasBonusOfType(BonusType::RETURN_AFTER_STRIKE)
		&& !attacker.hasBonusOfType(BonusType::NOT_ACTIVE)
		&& !attacker.hasBonusOfType(BonusType::BIND_EFFECT))
		attacker.setPosition(originalPosition);

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
	accountForNewElementalRebirthSpawns(rebirthSpawnIdsBefore, &attacker, true, damageCache, hb);
	accountForNewPrimalBurstHits(primalBurstFirstNewHit, referenceController, true, damageCache, hb);
	meleeSpecialtyAI::complete(*hb, attacker.unitId(), meleeSpecialtyAI::completedPrimaryMelee(ap));
}
