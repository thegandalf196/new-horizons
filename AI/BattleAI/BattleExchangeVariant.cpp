/*
 * BattleAI.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "SylvanTacticsProjection.h"
#include "DreadCavalryProjection.h"
#include "../../lib/battle/NewHorizonsForestAmbush.h"
#include "AerialRetaliationProjection.h"
#include "BracePrimaryState.h"
#include "BattleExchangeVariant.h"
#include "MeleeSpecialtyProjection.h"
#include "ExchangeInitiativeQueue.h"
#include "../../lib/battle/NewHorizonsOrdinaryStrike.h"
#include "BattleExchangeAftermath.h"
#include "PetrifyingBloodValue.h"
#include "AttackPreviewHitContext.h"
#include "../../lib/battle/NewHorizonsSoulHarvest.h"
#include "../../lib/battle/NewHorizonsManaLeeches.h"
#include "../../lib/battle/NewHorizonsSerpentsEmbrace.h"
#include "../../lib/battle/NewHorizonsParalyzingGaze.h"
#include "ParalyzingGazeProjection.h"
#include "DefiantDenialProjection.h"
#include "BattleShroudStrike.h"
#include "PrimaryFortuneReceipt.h"
#include "DamageCacheAdmission.h"
#include "ProjectedTargetHit.h"
#include "SelectedAttackReceipt.h"
#include "../../lib/battle/NewHorizonsOpportunist.h"
#include "BattleEvaluator.h"
#include "NewHorizonsHexOfPain.h"
#include "../../lib/CStack.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/battle/NewHorizonsArmorer.h"
#include "../../lib/battle/NewHorizonsShroud.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsPlague.h"

#include <chrono>
#include <utility>
#include "HypotheticBattle.h"
#include "HypotheticBattleTypes.h"
#include "StackWithBonuses.h"

using battleExchangeAftermath::projectNoQuarterAfterHit;
using battleExchangeAftermath::relentlessAssaultPrimaryTargetId;
using battleExchangeAftermath::projectRelentlessAssaultAttack;

namespace
{
class MovementExchangeScope
{
	using Clock = std::chrono::steady_clock;
	BattleMovementTiming::StageTiming * timing;
	Clock::time_point started;
public:
	explicit MovementExchangeScope(BattleMovementTiming::StageTiming * timing)
		: timing(timing), started(timing ? Clock::now() : Clock::time_point{})
	{}
	~MovementExchangeScope()
	{
		finish();
	}
	void finish()
	{
		if(timing)
		{
			++timing->calls;
			timing->totalMicroseconds += std::chrono::duration_cast<std::chrono::microseconds>(
				Clock::now() - started).count();
			timing = nullptr;
		}
	}
};

class ExchangeTimingScope
{
	using Clock = std::chrono::steady_clock;
	using ExchangeTiming = BattleMovementTiming::ExchangeTiming;
	using StageTiming = BattleMovementTiming::StageTiming;
	ExchangeTiming * timing;
	ExchangeTiming local;
	Clock::time_point started;
public:
	explicit ExchangeTimingScope(ExchangeTiming * timing)
		: timing(timing), started(timing ? Clock::now() : Clock::time_point{})
	{}
	ExchangeTimingScope(const ExchangeTimingScope &) = delete;
	ExchangeTimingScope & operator=(const ExchangeTimingScope &) = delete;
	StageTiming * stage(StageTiming ExchangeTiming::* member)
	{
		return timing ? &(local.*member) : nullptr;
	}
	DamageCacheProbeTiming * probeDamageCache()
	{
		return timing && timing->collectProbeDamageCache ? &local.targetProbeDamageCache : nullptr;
	}
	HypotheticLifecycleTiming * lifecycleSubsets()
	{
		return timing ? &local.lifecycleSubsets : nullptr;
	}
	template<typename Callable>
	decltype(auto) measure(StageTiming ExchangeTiming::* member, Callable && action)
	{
		MovementExchangeScope measurement(stage(member));
		return std::forward<Callable>(action)();
	}
	~ExchangeTimingScope()
	{
		if(!timing)
			return;
		local.body.calls = 1;
		local.body.totalMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
			Clock::now() - started).count();
		uint64_t classified = 0;
		for(auto member : {&ExchangeTiming::membership, &ExchangeTiming::projectionSetup,
			&ExchangeTiming::targetProbes, &ExchangeTiming::appliedAttacks,
			&ExchangeTiming::activationStart, &ExchangeTiming::activationEnd, &ExchangeTiming::roundAdvance})
		{
			const auto & sample = local.*member;
			auto & aggregate = (*timing).*member;
			aggregate.calls += sample.calls;
			aggregate.totalMicroseconds += sample.totalMicroseconds;
			classified += sample.totalMicroseconds;
		}
		timing->body.calls += local.body.calls;
		if(timing->collectProbeDamageCache)
			timing->targetProbeDamageCache.accumulate(local.targetProbeDamageCache);
		// Nested subsets are aggregated separately, never added to classified body time.
		for(auto member : {&ExchangeTiming::targetProbePreparation,
			&ExchangeTiming::targetProbeMainDamage, &ExchangeTiming::targetProbeDpsScore})
		{
			const auto & sample = local.*member;
			auto & aggregate = (*timing).*member;
			aggregate.calls += sample.calls;
			aggregate.totalMicroseconds += sample.totalMicroseconds;
		}
		for(auto member : {&HypotheticLifecycleTiming::activationExpiryRemoval,
			&HypotheticLifecycleTiming::activationGamblerRemoval, &HypotheticLifecycleTiming::roundTimedUpdates})
		{
			const auto & sample = local.lifecycleSubsets.*member;
			auto & aggregate = timing->lifecycleSubsets.*member;
			aggregate.accumulate(sample);
		}
		timing->body.totalMicroseconds += local.body.totalMicroseconds;
		for(auto context : {&HypotheticLifecycleTiming::activationExpiryWork,
			&HypotheticLifecycleTiming::activationGamblerWork, &HypotheticLifecycleTiming::roundTimedWork})
		{
			for(auto stage : {&HypotheticLifecycleTiming::EffectWorkTiming::stackedQuery,
				&HypotheticLifecycleTiming::EffectWorkTiming::rawQuery,
				&HypotheticLifecycleTiming::EffectWorkTiming::snapshotMaterialization,
				&HypotheticLifecycleTiming::EffectWorkTiming::selectiveRemovalQuery,
				&HypotheticLifecycleTiming::EffectWorkTiming::removalMutation,
				&HypotheticLifecycleTiming::EffectWorkTiming::roundAging})
			{
				const auto & sample = (local.lifecycleSubsets.*context).*stage;
				auto & aggregate = (timing->lifecycleSubsets.*context).*stage;
				aggregate.accumulate(sample);
			}
		}
		timing->remainderMicroseconds += local.body.totalMicroseconds - classified;
	}
};

template<typename Callable>
decltype(auto) measureMovementExchange(BattleMovementTiming * timing, Callable && action)
{
	MovementExchangeScope measurement(timing ? &timing->findMoveExchange : nullptr);
	return std::forward<Callable>(action)();
}

bool hasNightProwlerBonus(const battle::Unit * unit)
{
	return unit && unit->hasBonus(CSelector(newHorizonsShroud::isNightProwlerBonus));
}

uint32_t distToNearestReachableNeighbour(const ReachabilityInfo & reachability,
	const battle::Unit * attacker, const battle::Unit * defender)
{
	auto attackableHexes = defender->getHexes();
	if(attacker->doubleWide())
	{
		if(defender->doubleWide())
			attackableHexes.insert(battle::Unit::getHexes(defender->occupiedHex(), true, defender->unitSide()));
		else
			attackableHexes.insert(battle::Unit::getHexes(defender->getPosition(), true, defender->unitSide()));
	}
	attackableHexes.eraseIf([defender](const BattleHex & hex)
		{
			return hex.getY() != defender->getPosition().getY() || !hex.isAvailable();
		});

	uint32_t result = ReachabilityInfo::INFINITE_DIST;
	for(const auto & targetHex : attackableHexes)
		for(const auto & neighbour : targetHex.getNeighbouringTiles())
			if(reachability.isReachable(neighbour))
				result = std::min(result, reachability.distances.at(neighbour.toInt()));
	return result;
}

bool controlledBySamePlayer(const CBattleInfoCallback & battle,
	const battle::Unit * first, const battle::Unit * second)
{
	const auto firstOwner = battle.battleGetOwner(first);
	const auto secondOwner = battle.battleGetOwner(second);
	return firstOwner != PlayerColor::CANNOT_DETERMINE && firstOwner == secondOwner;
}


void refreshEvasiveShroud(HypotheticBattle & battle, uint32_t attackerUnitId)
{
	auto attacker = battle.getForUpdate(attackerUnitId);
	if(!attacker)
		return;
	attacker->removeUnitBonus(CSelector(newHorizonsShroud::isEvasiveShroudProtection));
	attacker->addUnitBonus({newHorizonsShroud::evasiveShroudProtection()});
}

void spendAmbusher(HypotheticBattle & battle, uint32_t attackerUnitId)
{
	if(auto attacker = battle.getForUpdate(attackerUnitId))
		attacker->addUnitBonus({newHorizonsShroud::ambusherSpentMarker()});
}

void spendShadowAssault(HypotheticBattle & battle, uint32_t targetUnitId, BattleSide attackingSide)
{
	if(auto target = battle.getForUpdate(targetUnitId))
		target->addUnitBonus({newHorizonsShroud::shadowAssaultSpentMarker(attackingSide)});
}
}

MoveTarget::MoveTarget()
	: positions(), cachedAttack(), score(EvaluationResult::INEFFECTIVE_SCORE)
{
	turnsToReach = 1;
}

void BattleExchangeVariant::trackDreadContinuation(const std::shared_ptr<HypotheticBattle> & battle,
	uint32_t actorId, DamageCache & damageCache, bool isOur)
{
	const auto dreadExtra = dreadCavalryAI::projectPending(battle, actorId, damageCache);
	dpsScore.ourDamageReduce += isOur ? dreadExtra.ownDamage : dreadExtra.enemyDamage;
	dpsScore.enemyDamageReduce += isOur ? dreadExtra.enemyDamage : dreadExtra.ownDamage;
}

float BattleExchangeVariant::trackAttack(
	const AttackPossibility & ap,
	std::shared_ptr<HypotheticBattle> hb,
	DamageCache & damageCache)
{
	if(!ap.attackerState)
	{
		logAi->trace("Skipping fake ap attack");
		return 0;
	}
	const auto * ambushTarget = hb->battleGetUnitByID(ap.attack.defender->unitId());
	if(newHorizonsForestAmbush::concealed(ambushTarget)
		&& hb->battleMatchOwner(ap.attack.attacker, ambushTarget))
		return 0;
	if(const auto refreshed = selectedAttackReceipt::refreshForestAmbush(ap, hb, damageCache))
		return trackAttack(*refreshed, hb, damageCache);
	std::optional<HypotheticBattle::ManaLeechReplay> manaLeechReplay;
	if(ap.effectPreview && ap.effectPreview->hasProjectedManaLeech())
	{
		manaLeechReplay = hb->prepareManaLeechReplay(*ap.effectPreview);
		if(!manaLeechReplay)
			return 0; // Same pre-mutation skip contract as an unavailable actor.
	}
	sylvanTacticsAI::StrikeProjection tactics(hb.get());
	const auto rebirthSpawnIdsBefore = hb->getElementalRebirthSpawnUnitIds();
	const auto primalBurstFirstNewHit = hb->getProjectedPrimalBurstHits().size();

	auto attacker = hb->getForUpdate(ap.attack.attacker->unitId());
	if(!attacker || attacker->armorerLastStandEndedActivation)
		return 0;

	const auto referenceController = hb->battleGetOwner(attacker.get());
	const auto originalPosition = attacker->getPosition();
	applySelectedNightProwler(*hb, *attacker, originalPosition, ap.from);
	if(!ap.overwatchHits.empty())
		hb->projectVoluntaryMovement(attacker->unitId(), ap.from);
	selectedAttackReceipt::restorePosition(ap, *attacker, *hb);

	float attackValue = ap.attackValue();
	SelectedAttackReceipt selected(ap);

	dpsScore.ourDamageReduce += ap.attackerDamageReduce + ap.collateralDamageReduce;
	dpsScore.enemyDamageReduce += ap.defenderDamageReduce + ap.shootersBlockedDmg;
	dpsScore.enemyDamageReduce += std::max(0.0f, ap.petrifyingBloodControlValue);
	dpsScore.ourDamageReduce += std::max(0.0f, -ap.petrifyingBloodControlValue);

	selected.captureDamage(*hb);
	const auto & affectedUnits = selected.affectedUnits();
	auto & finalDamage = selected.damage();

	// Reapply the selected preview strike-by-strike.  This is intentionally
	// separate from the final-state snapshots: Fortune aftermath belongs
	// between strikes (and between retaliation and the triggering strike).
	if(!ap.fortuneStrikes.empty())
	{
		int64_t projectedAttackerDamage = 0;
		const auto attackerDamage = finalDamage[attacker->unitId()];
		if(ap.preAttackDamage > 0)
			selectedAttackReceipt::applyCapturedDamage(*hb, attacker, ap.preAttackDamage);

		for(const auto & strike : ap.fortuneStrikes)
		{
			if(!aerialRetaliationAI::replayAdmission(*hb, strike)) continue;
			auto projectedAttacker = hb->getForUpdate(strike.attackerId);
			auto projectedDefender = hb->getForUpdate(strike.defenderId);
			auto projectedAttack = selectedAttackReceipt::attackForStrike(strike,
				projectedAttacker.get(), projectedDefender.get());
			const bool hasPrimaryHit = std::ranges::any_of(strike.hits, [&strike](const auto & hit)
			{
				return hit.first == strike.defenderId;
			});
			const bool shroudFlankingAttack = hasPrimaryHit
				&& hb->battleIsShroudFlankingAttack(projectedAttack);
			const auto attackingSide = shroudFlankingAttack
				? hb->playerToSide(hb->battleGetOwner(projectedAttacker.get())) : BattleSide::NONE;
			const auto * attackingHero = attackingSide == BattleSide::ATTACKER || attackingSide == BattleSide::DEFENDER
				? hb->battleGetFightingHero(attackingSide) : nullptr;
			const bool triggersAmbusher = shroudFlankingAttack
				&& newHorizonsShroud::ambusherDamagePercent(attackingHero, projectedAttacker.get()) > 0;
			const auto shadowAssaultSide = hasPrimaryHit
				? qualifyingShadowAssaultSide(*hb, projectedAttack) : std::optional<BattleSide>{};
			const bool triggersEvasiveShroud = shroudFlankingAttack
				&& newHorizonsShroud::hasEvasiveShroud(attackingHero);
			if(strike.protectIntercepted)
				hb->consumeHeroOrderProtectInterception(ap.attack.defender->unitId(), strike.defenderId);
			for(const auto & receipt : strike.defiantDenials)
				if(receipt.phase == DefiantDenialProjection::Phase::BEFORE_HITS)
					battleDefiantAI::replay(*hb, receipt);
			const bool gazeAdmitted = strike.paralyzingGazeAdmitted;
			const bool serpentBinding = newHorizonsSerpentsEmbrace::qualifiesAttack(*hb,
				projectedAttacker.get(), projectedDefender.get(), strike.shooting,
				strike.damageProvenance == battle::DamageProvenance::PHYSICAL_CREATURE,
				strike.bracePreemptive || strike.cleaveDamagePercent > 0 || ap.attack.preemptiveDamagePercent > 0);
			std::vector<std::pair<uint32_t, int64_t>> actualHits;
			actualHits.reserve(strike.hits.size());
			struct PendingRebirth
			{
				newHorizonsElementalRebirth::DeathSnapshot snapshot;
				bool cloneKilled = false;
				bool nativeRebirth = false;
			};
			std::map<uint32_t, PendingRebirth> pendingRebirths;
			bool enemyStackKilled = false;
			bool endedActiveActivationThisStrike = false;
			int64_t harvestCasualties = 0;
			for(const auto & [unitId, damage] : strike.hits)
			{
				auto target = hb->getForUpdate(unitId);
				const auto preparedHit = projectedTargetHit::prepare(*hb, *target, damage,
					{unitId, strike.eligibleForLastStand, strike.retaliation, strike.retaliation || strike.bracePreemptive});
				auto appliedDamage = preparedHit.damageToApply;
				const bool consumesBastion = newHorizonsCombatSkills::isPhysicalCreatureAttack(
					projectedAttacker.get(), strike.damageProvenance == battle::DamageProvenance::PHYSICAL_CREATURE)
					&& hb->battleHasBastionProtection(target.get());
				const bool harvestEligible = newHorizonsSoulHarvest::qualifies(*hb, *projectedAttacker,
					*target, strike.damageProvenance == battle::DamageProvenance::PHYSICAL_CREATURE);
				const bool harvestMayRebirth = harvestEligible && hb->hasReadyNativeRebirth(target.get());
				const int32_t harvestCountBefore = harvestEligible ? target->getCount() : 0;
				const auto wasAlive = target->alive();
				if(wasAlive && !pendingRebirths.contains(unitId))
				{
					if(const auto snapshot = hb->captureElementalRebirthSource(*target))
						pendingRebirths.emplace(unitId, PendingRebirth{
							*snapshot, target->isClone(), hb->hasReadyNativeRebirth(target.get())});
				}
				const auto healthBefore = target->getAvailableHealth();
				const auto projectedDamage = battleAIProjectDamage(target.get(), appliedDamage,
					strike.damageProvenance);
				tactics.damage(*hb, projectedAttack, *target, appliedDamage, strike.damageProvenance);
				if(wasAlive && !target->alive())
					hb->releaseSerpentBindingsForDeath(*target);
				if(harvestEligible)
					harvestCasualties += newHorizonsSoulHarvest::casualtiesAfterHit(*target, harvestCountBefore, harvestMayRebirth);
				if(strike.bracePreemptive && unitId == strike.defenderId)
					attackPreview::finishBracePrimaryState(*hb, strike.attackerId, unitId, healthBefore);
				for(const auto & receipt : strike.defiantDenials)
					if(receipt.phase == DefiantDenialProjection::Phase::AFTER_TARGET_HIT && receipt.targetId == unitId)
						battleDefiantAI::replay(*hb, receipt);
				if(projectedTargetHit::finishAfterDefiant(*hb, *target, preparedHit))
					endedActiveActivationThisStrike = true;
				if(consumesBastion)
					target->armorerBastionRound = hb->battleGetRound();
				const auto healthLoss = std::max<int64_t>(0,
					healthBefore - target->getAvailableHealth());
				if(unitId == attacker->unitId())
					projectedAttackerDamage += healthLoss;
				actualHits.emplace_back(unitId, appliedDamage);
				if(projectedDamage.healthLoss > 0)
				{
					for(const auto & [noQuarterTargetId, moraleActivations] : strike.noQuarterTargets)
						if(noQuarterTargetId == unitId)
							target->applyNoQuarter(moraleActivations, true);
					hb->recordBloodrageTransition(target, wasAlive);
					const bool luckAffectedTarget = unitId == strike.defenderId
						|| hb->getLuckRollRules().affectsAllTargets;
					if(wasAlive && !target->alive() && luckAffectedTarget
							&& hb->battleMatchOwner(projectedAttacker.get(), target.get()))
						enemyStackKilled = true;
				}
			}
			newHorizonsSoulHarvest::grant(*projectedAttacker, harvestCasualties);
			// Each projected striker spends its one-attack reward after its full hit set.
			// The movement bonus belongs to the moved stack, so an enemy preemptive
			// strike cannot consume it.
			if(!strike.hits.empty() && hasNightProwlerBonus(projectedAttacker.get()))
				projectedAttacker->removeUnitBonus(CSelector(newHorizonsShroud::isNightProwlerBonus));

			// Spend Ambusher on the accepted direct hit even if its attacker did not
			// survive; Evasive Shroud refreshes only for a surviving attacker.
			if(triggersAmbusher)
				projectedAttacker->addUnitBonus({newHorizonsShroud::ambusherSpentMarker()});
			if(shadowAssaultSide)
				spendShadowAssault(*hb, projectedDefender->unitId(), *shadowAssaultSide);
			if(triggersEvasiveShroud && projectedAttacker->alive())
			{
				projectedAttacker->removeUnitBonus(CSelector(newHorizonsShroud::isEvasiveShroudProtection));
				projectedAttacker->addUnitBonus({newHorizonsShroud::evasiveShroudProtection()});
			}
			if(strike.cleaveDamagePercent > 0)
				projectedAttacker->cleaveUsedThisActivation = true;
			if(strike.perfectMoment && !strike.retaliation)
			{
				const auto side = hb->playerToSide(hb->battleGetOwner(projectedAttacker.get()));
				auto fortune = hb->getSylvanLuckState(side);
				projectedAttack.luckyStrike = fortune.consumePerfectMoment();
				hb->setSylvanLuckState(side, fortune);
			}
			const auto primaryHit = std::ranges::find(actualHits, strike.defenderId,
				&std::pair<uint32_t, int64_t>::first);
			battleExchangeAftermath::applyPrimaryStatuses(*hb, projectedAttack,
				primaryHit == actualHits.end() ? 0 : primaryHit->second, serpentBinding, gazeAdmitted);
			tactics.finish(projectedAttack.attacker->unitId());
			hb->projectFortuneStrike(projectedAttack, actualHits, projectedAttacker.get(), enemyStackKilled,
				strike.resolvedLuck, true, strike.perfectFortune, strike.perfectFortuneSide,
				strike.luckSerendipitySide, strike.luckSerendipityOrdinaryAttack);
			hb->projectRangedMarkStrike(projectedAttack, actualHits);
			for(const auto & [unitId, pending] : pendingRebirths)
			{
				auto target = hb->getForUpdate(unitId);
				hb->projectElementalRebirth(target.get(), pending.snapshot,
					!target->alive(), pending.cloneKilled, pending.nativeRebirth);
			}
			const auto aerialReflectionLoss = aerialRetaliationAI::replaySourceAftermath(*hb, strike);
			if(projectedAttacker->unitId() == attacker->unitId()) projectedAttackerDamage += aerialReflectionLoss;
			const auto healthBeforeHexOfPain = projectedAttacker->getAvailableHealth();
			hb->projectHexOfPainStrike(projectedAttack, actualHits, strike.attackIndex);
			if(projectedAttacker->unitId() == attacker->unitId())
				projectedAttackerDamage += std::max<int64_t>(0,
					healthBeforeHexOfPain - projectedAttacker->getAvailableHealth());
			// Protect applies to the captured primary target of each blow, not
			// once to the whole multi-attack action. AttackPossibility snapshots
			// the redirected ID before damage, so a killed Protector still records
			// the actual first target; later strikes can correctly target the Ward.
			if(strike.relentlessAssaultEligible)
			{
				auto projectedHeroOrderAttack = ap.attack;
				projectedHeroOrderAttack.protectIntercepted = strike.protectIntercepted;
				const auto primaryTarget = strike.relentlessAssaultEligible
					? std::optional<uint32_t>(strike.defenderId) : std::nullopt;
				projectRelentlessAssaultAttack(*hb, projectedHeroOrderAttack, primaryTarget);
			}
			if(strike.gazeConsumption && !strike.aerialExcursion)
				paralyzingGazeAI::replay(*hb, *strike.gazeConsumption);
			if(endedActiveActivationThisStrike)
				break;
		}

		// A preview can contain damage sources which are not represented by a
		// physical strike (for example, a reaction). Apply that residual only
		// after replaying the known strikes so it cannot move a defender's
		// Fortune activation boundary or kill a retaliation attacker early.
		if(attackerDamage > projectedAttackerDamage + ap.preAttackDamage)
			selectedAttackReceipt::applyCapturedDamage(*hb, attacker,
				attackerDamage - projectedAttackerDamage - ap.preAttackDamage);
	}
	else
	{
		// Older/partial previews may not carry strike metadata. Preserve their
		// health projection, but do not invent Fortune transitions for them.
		for(const auto & affectedUnit : affectedUnits)
		{
			auto unitToUpdate = hb->getForUpdate(affectedUnit->unitId());
			auto damageDealt = finalDamage[unitToUpdate->unitId()];
			if(damageDealt > 0)
			{
				const bool wasAlive = unitToUpdate->alive();
				const auto rebirthSource = wasAlive
					? hb->captureElementalRebirthSource(*unitToUpdate) : std::nullopt;
				const bool nativeRebirth = hb->hasReadyNativeRebirth(unitToUpdate.get());
				unitToUpdate->damage(damageDealt);
				if(wasAlive && !unitToUpdate->alive())
					hb->releaseSerpentBindingsForDeath(*unitToUpdate);
				hb->recordBloodrageTransition(unitToUpdate, wasAlive);
				if(rebirthSource)
					hb->projectElementalRebirth(unitToUpdate.get(), *rebirthSource,
						wasAlive && !unitToUpdate->alive(), unitToUpdate->isClone(), nativeRebirth);
			}
		}
	}

	for(const auto & hit : ap.tacticsSecondaryHits)
		sylvanTacticsAI::replaySecondary(*hb, hit);

	if(hb->getActiveStackID() == static_cast<int32_t>(attacker->unitId()))
		attacker->consumeNoQuarterActivation();
	if(ap.bulwarkMireGripTriggered && attacker->alive())
	{
		attacker->bulwarkMireGripApplied = true;
		const int bulwarkSkillId = SecondarySkill::decode(std::string(newHorizonsBulwark::SKILL_ID));
		const Bonus slow(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
			BonusSource::OTHER, -2, BonusSourceID(SecondarySkill(bulwarkSkillId)));
		attacker->addUnitBonus({slow});
	}

	for(auto affectedUnit : affectedUnits)
	{
		auto unitToUpdate = hb->getForUpdate(affectedUnit->unitId());
		selectedAttackReceipt::restoreResources(ap, *affectedUnit, unitToUpdate, attacker, *hb);
		if(unitToUpdate->unitSide() == attacker->unitSide())
		{
			if(unitToUpdate->unitId() == attacker->unitId())
			{
#if BATTLE_TRACE_LEVEL>=1
				logAi->trace(
					"%s -> %s, ap retaliation, %s, dps: %lld",
					hb->getForUpdate(ap.attack.defender->unitId())->getDescription(),
					ap.attack.attacker->getDescription(),
					ap.attack.shooting ? "shot" : "mellee",
					damageDealt);
#endif
			}
			else
			{
#if BATTLE_TRACE_LEVEL>=1
				logAi->trace(
					"%s, ap collateral, dps: %lld",
					unitToUpdate->getDescription(),
					damageDealt);
#endif
			}
		}
		else
		{
			if(unitToUpdate->unitId() == ap.attack.defender->unitId())
			{
#if BATTLE_TRACE_LEVEL>=1
				logAi->trace(
					"%s -> %s, ap attack, %s, dps: %lld",
					attacker->getDescription(),
					ap.attack.defender->getDescription(),
					ap.attack.shooting ? "shot" : "mellee",
					damageDealt);
#endif
			}
			else
			{
#if BATTLE_TRACE_LEVEL>=1
				logAi->trace(
					"%s, ap enemy collateral, dps: %lld",
					unitToUpdate->getDescription(),
					damageDealt);
#endif
			}
		}
	}
	completeSelectedAttack(ap, hb, *attacker, originalPosition, manaLeechReplay,
		rebirthSpawnIdsBefore, primalBurstFirstNewHit, referenceController, damageCache);

	return attackValue;
}

float BattleExchangeVariant::trackAttack(
	std::shared_ptr<StackWithBonuses> attacker,
	std::shared_ptr<StackWithBonuses> defender,
	bool shooting,
	bool isOurAttack,
	DamageCache & damageCache,
	std::shared_ptr<HypotheticBattle> hb,
	bool evaluateOnly,
	bool allowRetaliation,
	std::optional<bool> * readyOverwatch,
	const BattleTargetProbeTiming * probeTiming,
	DamageCacheAdmission * mainProbeAdmission,
	BattleExchangeAttackResult * actionResult, bool * manaLeechSequenceSpent,
	std::vector<petrifyingBloodAI::Pending> * bloodSequence)
{
	if(actionResult)
		*actionResult = {};
	if(!attacker || attacker->armorerLastStandEndedActivation
		|| (newHorizonsForestAmbush::concealed(defender.get())
			&& hb->battleMatchOwner(attacker.get(), defender.get())))
		return 0;

	// A nonadditive target-probe prefix. Early reaction returns finalize by RAII;
	// applied/null-sink paths read no clocks and invalid actors keep zero samples.
	MovementExchangeScope preparationMeasurement(
		evaluateOnly && probeTiming ? probeTiming->preparation : nullptr);

	if(const auto reaction = trackFutureReaction(attacker, defender, shooting,
		isOurAttack, damageCache, hb, evaluateOnly, allowRetaliation, readyOverwatch))
	{
		if(actionResult)
			actionResult->completedAction = !evaluateOnly;
		return *reaction;
	}

	sylvanTacticsAI::StrikeProjection tactics(hb.get());
	const auto rebirthSpawnIdsBefore = hb->getElementalRebirthSpawnUnitIds();
	const auto primalBurstFirstNewHit = hb->getProjectedPrimalBurstHits().size();
	const auto referenceController = hb->battleGetOwner(attacker.get());
	static const std::string cachingStringBlocksRetaliation = "type_BLOCKS_RETALIATION";
	static const auto selectorBlocksRetaliation = Selector::type()(BonusType::BLOCKS_RETALIATION);
	static const auto firstStrikeSelector = Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeAll)
		.Or(Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeMelee));
	const bool counterAttacksBlocked = !evaluateOnly && attacker->hasBonus(selectorBlocksRetaliation, cachingStringBlocksRetaliation);
	const auto requestedDefender = defender;
	BattleAttackInfo requestedAttack(attacker.get(), requestedDefender.get(), 0, shooting);
	const auto relentlessAssaultTargetUnitId = !evaluateOnly ? relentlessAssaultPrimaryTargetId(*hb, requestedAttack) : std::nullopt;
	const auto * redirectedDefender = hb->battleResolveHeroOrderTarget(attacker.get(), requestedDefender.get(), shooting);
	const bool protectIntercepted = !shooting && redirectedDefender
		&& redirectedDefender->unitId() != requestedDefender->unitId();
	if(protectIntercepted && !evaluateOnly)
		hb->consumeHeroOrderProtectInterception(requestedDefender->unitId(), redirectedDefender->unitId());
	requestedAttack.protectIntercepted = protectIntercepted;
	if(protectIntercepted)
	{
		defender = hb->getForUpdate(redirectedDefender->unitId());
		if(!defender)
			return 0;
	}
	BattleAttackInfo projectedAttack(attacker.get(), defender.get(), 0, shooting);
	const bool hostilePrimaryMelee = !shooting && hb->battleGetOwner(attacker.get()) != hb->battleGetOwner(defender.get());
	projectedAttack.protectIntercepted = protectIntercepted;
	const auto manaLeech = !evaluateOnly && (!manaLeechSequenceSpent || !*manaLeechSequenceSpent)
		? newHorizonsManaLeeches::entitlement(*hb, attacker.get(), defender.get(), shooting,
			projectedAttack.physicalDamage, false) : newHorizonsManaLeeches::Entitlement{};
	const bool serpentBinding = !evaluateOnly && newHorizonsSerpentsEmbrace::qualifiesAttack(*hb,
		attacker.get(), defender.get(), shooting, projectedAttack.physicalDamage, false);
	const bool gazeAdmitted = !evaluateOnly && newHorizonsParalyzingGaze::qualifiesAttack(*hb,
		attacker.get(), defender.get(), shooting, projectedAttack.physicalDamage, false);
	const auto primaryFortune = captureAppliedFortuneReceipt(*hb, projectedAttack, evaluateOnly);
	const auto shroud = !evaluateOnly ? evaluateBattleShroudStrike(*hb, projectedAttack) : BattleShroudStrike{};
	const bool triggersEvasiveShroud = shroud.evasiveShroud;
	const bool triggersAmbusher = shroud.ambusher;
	const auto shadowAssaultSide = shroud.shadowAssaultSide;
	const bool nightProwlerPending = !evaluateOnly && hasNightProwlerBonus(attacker.get());
	const bool frozenBeforeHit = !evaluateOnly && newHorizonsFrozen::isFrozen(*defender);

	preparationMeasurement.finish();
	int64_t attackDamage;
	{
		MovementExchangeScope damageMeasurement(evaluateOnly && probeTiming ? probeTiming->mainDamage : nullptr);
		attackDamage = damageCache.getDamage(attacker.get(), defender.get(), hb,
			evaluateOnly && probeTiming ? probeTiming->damageCache : nullptr,
			evaluateOnly ? mainProbeAdmission : nullptr);
	}
	const auto attackLastStand = AttackPreviewHitContext::resolveUnspentLastStand(*hb, projectedAttack, defender.get(), attackDamage);
	attackDamage = attackLastStand.first.damageToApply;
	const auto attackDamageProvenance = battleAIDamageProvenance(
		attacker.get(), projectedAttack.physicalDamage);
	const bool consumesBastion = !evaluateOnly && newHorizonsCombatSkills::isPhysicalCreatureAttack(
		projectedAttack.attacker, projectedAttack.physicalDamage)
		&& hb->battleHasBastionProtection(defender.get());
	const auto projectedAttackDamage = battleAIProjectDamage(
		defender.get(), attackDamage, attackDamageProvenance);
	const int64_t actualDamage = projectedAttackDamage.appliedDamage;
	float defenderDamageReduce;
	{
		MovementExchangeScope scoreMeasurement(evaluateOnly && probeTiming ? probeTiming->dpsScore : nullptr);
		defenderDamageReduce = AttackPossibility::calculateDamageReduce(attacker.get(), defender.get(),
			projectedAttackDamage.healthLoss, damageCache, hb,
			evaluateOnly && probeTiming ? probeTiming->damageCache : nullptr);
	}
	float attackerDamageReduce = 0;
	std::vector<petrifyingBloodAI::Pending> localBloodSequence;
	auto & petrifyingBloodPending = bloodSequence ? *bloodSequence : localBloodSequence;
	const bool harvestEligible = !evaluateOnly && newHorizonsSoulHarvest::qualifies(*hb,
		*attacker, *defender, projectedAttack.physicalDamage);
	const bool defenderWasAlive = defender->alive();
	const int32_t defenderHarvestCountBefore = harvestEligible ? defender->getCount() : 0;
	const int64_t defenderHealthBeforeAttack = defender->getAvailableHealth();
	const auto defenderRebirthSource = !evaluateOnly && defenderWasAlive
		? hb->captureElementalRebirthSource(*defender) : std::nullopt;
	// Applied attacks retain their pre-hit native-Rebirth receipt. A probe
	// needs this query only if its post-attack state can actually trigger Cleave.
	const bool defenderMayRebirth = !evaluateOnly && hb->hasReadyNativeRebirth(defender.get());
	// Only Hex and Cleave consume the probe's post-attack attacker state.
	// Ordinary applied attacks must still spend shots/Wait bonuses as before.
	const bool needsProbeState = evaluateOnly
		&& (newHorizonsHexOfPainAI::hasEffect(attacker.get())
			|| (!shooting && hb->battleCanTriggerCleave(attacker.get())));
	auto projectedAttacker = !evaluateOnly
		? std::static_pointer_cast<battle::CUnitState>(attacker)
		: needsProbeState ? attacker->acquireState() : nullptr;

	if(!evaluateOnly)
	{
#if BATTLE_TRACE_LEVEL>=1
		logAi->trace(
			"%s -> %s, normal attack, %s, dps: %lld, %2f",
			attacker->getDescription(),
			defender->getDescription(),
			shooting ? "shot" : "mellee",
			attackDamage,
			defenderDamageReduce);
#endif

		if(isOurAttack)
		{
			dpsScore.enemyDamageReduce += defenderDamageReduce;
		}
		else
			dpsScore.ourDamageReduce += defenderDamageReduce;

		tactics.damage(*hb, projectedAttack, *defender, attackDamage, attackDamageProvenance);
		petrifyingBloodAI::capture(hb, *attacker, *defender, actualDamage,
			shooting, projectedAttack.physicalDamage, petrifyingBloodPending);
		if(defenderWasAlive && !defender->alive())
			hb->releaseSerpentBindingsForDeath(*defender);
		hb->projectSerpentBinding(attacker->unitId(), defender->unitId(), actualDamage, serpentBinding);
		if(harvestEligible)
			newHorizonsSoulHarvest::grant(*attacker, newHorizonsSoulHarvest::casualtiesAfterHit(
				*defender, defenderHarvestCountBefore, defenderMayRebirth));
		paralyzingGazeAI::apply(*hb, attacker->unitId(), defender->unitId(), actualDamage, gazeAdmitted);
		AttackPreviewHitContext::applyOrdinaryStrike(*hb, projectedAttack, actualDamage);
		if(manaLeech.amount > 0)
			hb->projectManaLeech(manaLeech, actualDamage, manaLeechSequenceSpent);
		AttackPreviewHitContext::applyUnspentLastStand(*hb, defender.get(), attackLastStand.first, attackLastStand.second,
			projectedAttack.retaliation, !evaluateOnly);
		if(nightProwlerPending)
			attacker->removeUnitBonus(CSelector(newHorizonsShroud::isNightProwlerBonus));
		if(triggersAmbusher)
			spendAmbusher(*hb, attacker->unitId());
		if(shadowAssaultSide)
			spendShadowAssault(*hb, defender->unitId(), *shadowAssaultSide);
		if(triggersEvasiveShroud && attacker->alive())
			refreshEvasiveShroud(*hb, attacker->unitId());
		if(consumesBastion)
			defender->armorerBastionRound = hb->battleGetRound();
		if(actualDamage > 0)
			projectNoQuarterAfterHit(*hb, projectedAttack, *defender);
		hb->recordBloodrageTransition(defender, defenderWasAlive);
		tactics.finish(projectedAttack.attacker->unitId());
		hb->projectFortuneStrike(projectedAttack, {{defender->unitId(), actualDamage}}, attacker.get(),
			defenderWasAlive && !defender->alive() && hb->battleMatchOwner(attacker.get(), defender.get()),
			primaryFortune->resolvedLuck, true, primaryFortune->perfectFortune,
			primaryFortune->controllerSide, primaryFortune->controllerSide, primaryFortune->ordinaryLuckAttack);
		hb->projectRangedMarkStrike(projectedAttack, {{defender->unitId(), actualDamage}});
		if(defenderRebirthSource)
			hb->projectElementalRebirth(defender.get(), *defenderRebirthSource,
				defenderWasAlive && !defender->alive(), defender->isClone(), defenderMayRebirth);
		if(relentlessAssaultTargetUnitId)
			projectRelentlessAssaultAttack(*hb, requestedAttack, relentlessAssaultTargetUnitId);
	}
	if(projectedAttacker)
		projectedAttacker->afterAttack(shooting, false, projectedAttack.physicalDamage);
	if(actionResult && !evaluateOnly && hostilePrimaryMelee)
		actionResult->primaryMeleePerformed = true;
	if(!evaluateOnly && shooting && projectedAttack.physicalDamage
		&& attacker->rangedFollowUpDamagePercent > 0)
		attacker->rangedFollowUpDamagePercent = 0;
	const auto projectHexOfPain = [&](const battle::Unit * actor, const battle::Unit * target,
		bool isShooting, bool isCounter, const std::vector<std::pair<uint32_t, int64_t>> & hits)
	{
		if(!actor || !newHorizonsHexOfPainAI::hasEffect(actor) || hits.empty())
			return int64_t{0};

		auto eventBattle = evaluateOnly
			? std::make_shared<HypotheticBattle>(hb->env, hb)
			: hb;
		auto eventActor = eventBattle->getForUpdate(actor->unitId());
		auto eventTarget = target ? eventBattle->getForUpdate(target->unitId()) : nullptr;
		BattleAttackInfo eventAttack(eventActor.get(), eventTarget.get(), 0, isShooting);
		eventAttack.retaliation = isCounter;
		return eventBattle->projectHexOfPainStrike(eventAttack, hits);
	};
	const auto recordHexOfPain = [&](const battle::Unit * actorBeforeTrigger, int64_t damage)
	{
		if(!actorBeforeTrigger || damage <= 0)
			return;

		const auto value = AttackPossibility::calculateDamageReduce(nullptr, actorBeforeTrigger,
			static_cast<uint64_t>(damage), damageCache, hb);
		const bool sameSideAsMainAttacker = hb->battleMatchOwner(attacker.get(), actorBeforeTrigger, true);
		if(sameSideAsMainAttacker)
			attackerDamageReduce += value;
		else
			defenderDamageReduce += value;

		const bool actorOnOurSide = sameSideAsMainAttacker == isOurAttack;
		if(actorOnOurSide)
			dpsScore.ourDamageReduce += value;
		else
			dpsScore.enemyDamageReduce += value;
	};
	if(projectedAttacker && newHorizonsHexOfPainAI::hasEffect(projectedAttacker.get()))
	{
		const auto mainHexScoringActor = projectedAttacker->acquireState();
		const auto mainHexDamage = projectHexOfPain(projectedAttacker.get(), defender.get(), shooting, false,
			{{defender->unitId(), actualDamage}});
		recordHexOfPain(mainHexScoringActor.get(), mainHexDamage);
	}

	const bool projectedEnemyKill = defenderWasAlive && !defenderMayRebirth
		&& (evaluateOnly
			? projectedAttackDamage.healthLoss >= defenderHealthBeforeAttack
			: !defender->alive());
	if(!shooting && projectedAttacker && projectedAttacker->alive() && projectedEnemyKill
		&& hb->battleMatchOwner(attacker.get(), defender.get())
		&& hb->battleCanTriggerCleave(projectedAttacker.get())
		&& (!evaluateOnly || !hb->hasReadyNativeRebirth(defender.get())))
	{
		if(const auto * selected = hb->battleSelectCleaveTarget(projectedAttacker.get(), defender.get()))
		{
			auto target = evaluateOnly
				? std::shared_ptr<StackWithBonuses>{}
				: hb->getForUpdate(selected->unitId());
			const battle::Unit * targetUnit = target ? target.get() : selected;
			BattleAttackInfo cleaveAttack(projectedAttacker.get(), targetUnit, 0, false);
			cleaveAttack.attackerPos = projectedAttacker->getPosition();
			cleaveAttack.defenderPos = targetUnit->getPosition();
			cleaveAttack.cleaveDamagePercent = newHorizonsOffense::CLEAVE_DAMAGE_PERCENT;
			const auto cleaveShroud = !evaluateOnly ? evaluateBattleShroudStrike(*hb, cleaveAttack) : BattleShroudStrike{};
			const bool triggersCleaveEvasiveShroud = cleaveShroud.evasiveShroud;
			const bool triggersCleaveAmbusher = cleaveShroud.ambusher;
			const auto cleaveShadowAssaultSide = cleaveShroud.shadowAssaultSide;
			const auto cleaveProvenance = battleAIDamageProvenance(
				projectedAttacker.get(), cleaveAttack.physicalDamage);
			const bool consumesCleaveBastion = newHorizonsCombatSkills::isPhysicalCreatureAttack(
				cleaveAttack.attacker, cleaveAttack.physicalDamage)
				&& hb->battleHasBastionProtection(targetUnit);
			projectedAttacker->cleaveUsedThisActivation = true;

			const bool cleavePerfectFortune = hb->battleCanTriggerPerfectFortune(cleaveAttack);
			const auto cleavePerfectFortuneSide = hb->playerToSide(hb->battleGetActionController(cleaveAttack.attacker));
			const bool cleaveSerendipityOrdinaryAttack = !cleaveAttack.secondaryAttack
				&& newHorizonsCombatSkills::isPhysicalCreatureLuckAttack(cleaveAttack.attacker, cleaveAttack.physicalDamage);
			const auto cleaveResolvedLuck = hb->captureFortuneStrikeOutcome(cleaveAttack);
			int64_t cleaveDamage = hb->battleExpectedLuckDamage(cleaveAttack);
			const auto cleaveLastStand = AttackPreviewHitContext::resolveUnspentLastStand(*hb, cleaveAttack, targetUnit, cleaveDamage);
			cleaveDamage = cleaveLastStand.first.damageToApply;
			const auto projectedCleaveDamage = battleAIProjectDamage(
				targetUnit, cleaveDamage, cleaveProvenance);
			const float cleaveDamageReduce = AttackPossibility::calculateDamageReduce(
				projectedAttacker.get(), targetUnit, projectedCleaveDamage.healthLoss, damageCache, hb);
			defenderDamageReduce += cleaveDamageReduce;
			int64_t resolvedCleaveDamage = projectedCleaveDamage.appliedDamage;

			if(!evaluateOnly)
			{
				if(isOurAttack)
				{
					dpsScore.enemyDamageReduce += cleaveDamageReduce;
				}
				else
					dpsScore.ourDamageReduce += cleaveDamageReduce;

				const bool cleaveHarvestEligible = newHorizonsSoulHarvest::qualifies(*hb,
					*projectedAttacker, *target, cleaveAttack.physicalDamage);
				const bool targetWasAlive = target->alive();
				const int32_t targetHarvestCountBefore = cleaveHarvestEligible ? target->getCount() : 0;
				const auto targetRebirthSource = targetWasAlive
					? hb->captureElementalRebirthSource(*target) : std::nullopt;
				const bool targetMayRebirth = hb->hasReadyNativeRebirth(target.get());
				tactics.damage(*hb, cleaveAttack, *target, cleaveDamage, cleaveProvenance);
				petrifyingBloodAI::capture(hb, *projectedAttacker, *target, resolvedCleaveDamage,
					false, cleaveAttack.physicalDamage, petrifyingBloodPending);
				if(targetWasAlive && !target->alive())
					hb->releaseSerpentBindingsForDeath(*target);
				if(cleaveHarvestEligible)
					newHorizonsSoulHarvest::grant(*projectedAttacker, newHorizonsSoulHarvest::casualtiesAfterHit(
						*target, targetHarvestCountBefore, targetMayRebirth));
				AttackPreviewHitContext::applyUnspentLastStand(*hb, target.get(), cleaveLastStand.first,
					cleaveLastStand.second, cleaveAttack.retaliation, !evaluateOnly);
				if(triggersCleaveAmbusher)
					spendAmbusher(*hb, projectedAttacker->unitId());
				if(cleaveShadowAssaultSide)
					spendShadowAssault(*hb, target->unitId(), *cleaveShadowAssaultSide);
				if(triggersCleaveEvasiveShroud && projectedAttacker->alive())
					refreshEvasiveShroud(*hb, projectedAttacker->unitId());
				if(consumesCleaveBastion)
					target->armorerBastionRound = hb->battleGetRound();
				resolvedCleaveDamage = cleaveDamage;
				if(resolvedCleaveDamage > 0)
					projectNoQuarterAfterHit(*hb, cleaveAttack, *target);
				hb->recordBloodrageTransition(target, targetWasAlive);
				tactics.finish(cleaveAttack.attacker->unitId());
				hb->projectFortuneStrike(cleaveAttack, {{target->unitId(), cleaveDamage}}, attacker.get(),
					targetWasAlive && !target->alive() && hb->battleMatchOwner(attacker.get(), target.get()),
					cleaveResolvedLuck, true, cleavePerfectFortune, cleavePerfectFortuneSide,
					cleavePerfectFortuneSide, cleaveSerendipityOrdinaryAttack);
				hb->projectRangedMarkStrike(cleaveAttack, {{target->unitId(), cleaveDamage}});
				if(targetRebirthSource)
					hb->projectElementalRebirth(target.get(), *targetRebirthSource,
						targetWasAlive && !target->alive(), target->isClone(), targetMayRebirth);
			}
				projectedAttacker->afterAttack(false, false, cleaveAttack.physicalDamage);
				if(newHorizonsHexOfPainAI::hasEffect(projectedAttacker.get()))
				{
					const auto cleaveHexScoringActor = projectedAttacker->acquireState();
					const auto cleaveHexDamage = projectHexOfPain(projectedAttacker.get(), targetUnit,
						false, false, {{targetUnit->unitId(), resolvedCleaveDamage}});
					recordHexOfPain(cleaveHexScoringActor.get(), cleaveHexDamage);
				}
			}
	}

	const auto [ignoredInnate, ignoredShroud] = battleDefiantAI::projectMeleeDenials(
		hb.get(), projectedAttack, counterAttacksBlocked,
		!frozenBeforeHit && !evaluateOnly && allowRetaliation && !shooting, firstStrikeSelector);
	if(!frozenBeforeHit && !evaluateOnly && allowRetaliation && attacker->alive() && defender->alive()
		&& defender->ableToRetaliate() && !shooting
		&& (!counterAttacksBlocked || (ignoredInnate && !hb->battleHasMagicalRetaliationBlock(attacker.get())))
		&& (!hb->battleShroudDeniesRetaliation(projectedAttack) || defender->hasBonus(firstStrikeSelector) || ignoredShroud)
		&& paralyzingGazeAI::consumeAndAllowCounter(*hb, attacker->unitId(), defender->unitId(), false))
	{
		BattleAttackInfo retaliationAttack(defender.get(), attacker.get(), 0, false);
		retaliationAttack.retaliation = true;
		const bool counterSerpentBinding = newHorizonsSerpentsEmbrace::qualifiesAttack(*hb,
			defender.get(), attacker.get(), false, retaliationAttack.physicalDamage, false);
		const auto counterManaLeech = newHorizonsManaLeeches::entitlement(*hb,
			defender.get(), attacker.get(), false, retaliationAttack.physicalDamage, false);
		const bool retaliationPerfectFortune = hb->battleCanTriggerPerfectFortune(retaliationAttack);
		const auto retaliationPerfectFortuneSide = hb->playerToSide(hb->battleGetActionController(retaliationAttack.attacker));
		const bool retaliationSerendipityOrdinaryAttack = newHorizonsCombatSkills::isPhysicalCreatureLuckAttack(
			retaliationAttack.attacker, retaliationAttack.physicalDamage);
		const auto retaliationResolvedLuck = hb->captureFortuneStrikeOutcome(retaliationAttack);
		const auto retaliationShroud = evaluateBattleShroudStrike(*hb, retaliationAttack);
		const bool triggersRetaliationEvasiveShroud = retaliationShroud.evasiveShroud;
		const bool triggersRetaliationAmbusher = retaliationShroud.ambusher;
		const auto retaliationShadowAssaultSide = retaliationShroud.shadowAssaultSide;
		const bool retaliationNightProwlerPending = hasNightProwlerBonus(defender.get());
		auto retaliationDamage = hb->battleExpectedLuckDamage(retaliationAttack);
		const auto retaliationLastStand = AttackPreviewHitContext::resolveUnspentLastStand(
			*hb, retaliationAttack, attacker.get(), retaliationDamage);
		retaliationDamage = retaliationLastStand.first.damageToApply;
		const bool consumesRetaliationBastion = newHorizonsCombatSkills::isPhysicalCreatureAttack(
			retaliationAttack.attacker, retaliationAttack.physicalDamage)
			&& hb->battleHasBastionProtection(attacker.get());
		const auto retaliationProvenance = battleAIDamageProvenance(
			defender.get(), retaliationAttack.physicalDamage);
		const auto projectedRetaliationDamage = battleAIProjectDamage(
			attacker.get(), retaliationDamage, retaliationProvenance);
		attackerDamageReduce = AttackPossibility::calculateDamageReduce(defender.get(), attacker.get(),
			projectedRetaliationDamage.healthLoss, damageCache, hb);

#if BATTLE_TRACE_LEVEL>=1
		logAi->trace(
			"%s -> %s, retaliation, dps: %lld, %2f",
			defender->getDescription(),
			attacker->getDescription(),
			retaliationDamage,
			attackerDamageReduce);
#endif

		if(isOurAttack)
		{
			dpsScore.ourDamageReduce += attackerDamageReduce;
		}
		else
		{
			dpsScore.enemyDamageReduce += attackerDamageReduce;
		}

		const int64_t actualDamage = projectedRetaliationDamage.appliedDamage;
		const bool counterHarvestEligible = newHorizonsSoulHarvest::qualifies(*hb,
			*defender, *attacker, retaliationAttack.physicalDamage);
		const bool attackerWasAlive = attacker->alive();
		const int32_t attackerHarvestCountBefore = counterHarvestEligible ? attacker->getCount() : 0;
		const auto attackerRebirthSource = attackerWasAlive
			? hb->captureElementalRebirthSource(*attacker) : std::nullopt;
		const bool attackerMayRebirth = hb->hasReadyNativeRebirth(attacker.get());
		tactics.damage(*hb, retaliationAttack, *attacker, retaliationDamage, retaliationProvenance);
		petrifyingBloodAI::capture(hb, *defender, *attacker, actualDamage,
			false, retaliationAttack.physicalDamage, petrifyingBloodPending);
		if(attackerWasAlive && !attacker->alive())
			hb->releaseSerpentBindingsForDeath(*attacker);
		hb->projectSerpentBinding(defender->unitId(), attacker->unitId(), actualDamage, counterSerpentBinding);
		AttackPreviewHitContext::applyOrdinaryStrike(*hb, retaliationAttack, actualDamage);
		if(counterHarvestEligible)
			newHorizonsSoulHarvest::grant(*defender, newHorizonsSoulHarvest::casualtiesAfterHit(
				*attacker, attackerHarvestCountBefore, attackerMayRebirth));
		if(counterManaLeech.amount > 0)
			hb->projectManaLeech(counterManaLeech, actualDamage, nullptr);
		AttackPreviewHitContext::applyUnspentLastStand(*hb, attacker.get(), retaliationLastStand.first,
			retaliationLastStand.second, retaliationAttack.retaliation, !evaluateOnly);
		if(retaliationNightProwlerPending)
			defender->removeUnitBonus(CSelector(newHorizonsShroud::isNightProwlerBonus));
		if(triggersRetaliationAmbusher)
			spendAmbusher(*hb, defender->unitId());
		if(retaliationShadowAssaultSide)
			spendShadowAssault(*hb, attacker->unitId(), *retaliationShadowAssaultSide);
		if(triggersRetaliationEvasiveShroud && defender->alive())
			refreshEvasiveShroud(*hb, defender->unitId());
		if(consumesRetaliationBastion)
			attacker->armorerBastionRound = hb->battleGetRound();
		if(actualDamage > 0)
			projectNoQuarterAfterHit(*hb, retaliationAttack, *attacker);
		hb->recordBloodrageTransition(attacker, attackerWasAlive);
		tactics.finish(retaliationAttack.attacker->unitId());
		hb->projectFortuneStrike(retaliationAttack, {{attacker->unitId(), actualDamage}}, defender.get(),
			attackerWasAlive && !attacker->alive() && hb->battleMatchOwner(defender.get(), attacker.get()),
			retaliationResolvedLuck, true, retaliationPerfectFortune, retaliationPerfectFortuneSide,
			retaliationPerfectFortuneSide, retaliationSerendipityOrdinaryAttack);
		if(attackerRebirthSource)
			hb->projectElementalRebirth(attacker.get(), *attackerRebirthSource,
				attackerWasAlive && !attacker->alive(), attacker->isClone(), attackerMayRebirth);
		defender->afterAttack(false, true, retaliationAttack.physicalDamage);
		if(newHorizonsHexOfPainAI::hasEffect(defender.get()))
		{
			const auto retaliationHexScoringActor = defender->acquireState();
			const auto retaliationHexDamage = projectHexOfPain(defender.get(), attacker.get(), false, true,
				{{attacker->unitId(), actualDamage}});
			recordHexOfPain(retaliationHexScoringActor.get(), retaliationHexDamage);
		}
	}
	if(!evaluateOnly && hb->getActiveStackID() == static_cast<int32_t>(attacker->unitId()))
		attacker->consumeNoQuarterActivation();

	const float petrifyingBloodControl = !evaluateOnly && !bloodSequence
		? finishPetrifyingBlood(hb, *attacker, petrifyingBloodPending, isOurAttack, damageCache) : 0;
	auto score = defenderDamageReduce - attackerDamageReduce + petrifyingBloodControl;

#if BATTLE_TRACE_LEVEL>=1
	if(!score)
	{
		logAi->trace("Attack has zero score def:%2f att:%2f", defenderDamageReduce, attackerDamageReduce);
	}
#endif
	accountForNewElementalRebirthSpawns(rebirthSpawnIdsBefore, attacker.get(), isOurAttack, damageCache, hb);
	accountForNewPrimalBurstHits(primalBurstFirstNewHit, referenceController, isOurAttack, damageCache, hb);

	return score;
}

float BattleExchangeEvaluator::scoreValue(const BattleScore & score) const
{
	return score.enemyDamageReduce * getPositiveEffectMultiplier() - score.ourDamageReduce * getNegativeEffectMultiplier();
}

EvaluationResult BattleExchangeEvaluator::findBestTarget(
	const battle::Unit * activeStack,
	PotentialTargets & targets,
	DamageCache & damageCache,
	std::shared_ptr<HypotheticBattle> hb,
	bool siegeDefense,
	BattleBestTargetTiming * timing)
{
	EvaluationResult result(targets.bestAction());

	if(!activeStack->acquireState()->waitedThisTurn && !activeStack->acquireState()->hadMorale)
	{
#if BATTLE_TRACE_LEVEL>=1
		logAi->trace("Evaluating waited attack for %s", activeStack->getDescription());
#endif

		auto hbWaited = std::make_shared<HypotheticBattle>(env.get(), hb);

		hbWaited->makeWait(activeStack);

		updateReachabilityMap(hbWaited);

		for(auto & ap : targets.possibleAttacks)
		{
			if (siegeDefense && !hb->battleIsInsideWalls(ap.from))
				continue;

			float score;
			{
				MovementExchangeScope measurement(timing ? &timing->waitedExchange : nullptr);
				score = evaluateExchange(ap, 0, targets, damageCache, hbWaited,
					timing ? &timing->waitedDetails : nullptr);
			}

			if(score > result.score)
			{
				result.score = score;
				result.bestAttack = ap;
				result.wait = true;

#if BATTLE_TRACE_LEVEL >= 1
				logAi->trace("New high score %2f", result.score);
#endif
			}
		}
	}

#if BATTLE_TRACE_LEVEL>=1
	logAi->trace("Evaluating normal attack for %s", activeStack->getDescription());
#endif

	updateReachabilityMap(hb);

	if(result.bestAttack.attack.shooting
		&& !result.bestAttack.defenderDead
		&& !activeStack->acquireState()->waitedThisTurn
		&& hb->battleHasShootingPenalty(activeStack, result.bestAttack.dest))
	{
		if(!canBeHitThisTurn(result.bestAttack))
			return result; // lets wait
	}

	for(auto & ap : targets.possibleAttacks)
	{
		if (siegeDefense && !hb->battleIsInsideWalls(ap.from))
			continue;

		float score;
		{
			MovementExchangeScope measurement(timing ? &timing->normalExchange : nullptr);
			score = evaluateExchange(ap, 0, targets, damageCache, hb,
				timing ? &timing->normalDetails : nullptr);
		}
		bool sameScoreButWaited = vstd::isAlmostEqual(score, result.score) && result.wait;

		if(score > result.score || sameScoreButWaited)
		{
			result.score = score;
			result.bestAttack = ap;
			result.wait = false;

#if BATTLE_TRACE_LEVEL >= 1
			logAi->trace("New high score %2f", result.score);
#endif
		}
	}

	return result;
}

MoveTarget BattleExchangeEvaluator::findMoveTowardsUnreachable(
	const battle::Unit * activeStack,
	PotentialTargets & targets,
	DamageCache & damageCache,
	std::shared_ptr<HypotheticBattle> hb,
	BattleMovementTiming * timing)
{
	MovementExchangeScope measurement(timing ? &timing->findMove : nullptr);
	MoveTarget result;
	if(targets.berserk)
	{
		// A detached forecast must retain Berserk's selected path. Do not replace
		// its forced destination with the ordinary tactical target chooser.
		for(const auto & forcedAction : targets.forcedBerserkActions)
			if(forcedAction.type == EActionType::WALK && forcedAction.position.isValid())
				result.positions.insert(forcedAction.position);

		if(!result.positions.empty())
		{
			result.score = 0.0f;
			result.turnsToReach = 1;
		}
		return result;
	}

	logAi->trace("Find move towards unreachable. Enemies count %d", targets.unreachableEnemies.size());

	if(targets.unreachableEnemies.empty())
		return result;

	auto speed = activeStack->getMovementRange();

	if(speed == 0)
		return result;

	updateReachabilityMap(hb);

	auto dists = getReachabilityWithEnemyBypass(activeStack, damageCache, hb);
	auto flying = activeStack->hasBonusOfType(BonusType::FLYING);

	for(const battle::Unit * enemy : targets.unreachableEnemies)
	{
		logAi->trace(
			"Checking movement towards %d of %s",
			enemy->getCount(),
			LIBRARY->creatures()->getById(enemy->creatureId())->getJsonKey());

		auto distance = distToNearestReachableNeighbour(dists, activeStack, enemy);

		if(distance >= GameConstants::BFIELD_SIZE)
			continue;

		if(distance <= speed)
			continue;

		float penaltyMultiplier = 1.0f; // Default multiplier, no penalty
		float closestAllyDistance = std::numeric_limits<float>::max();

		for (const battle::Unit* ally : hb->battleAliveUnits()) 
		{
			if (ally == activeStack || !controlledBySamePlayer(*hb, activeStack, ally))
				continue;

			float allyDistance = distToNearestReachableNeighbour(dists, ally, enemy);
			if (allyDistance < closestAllyDistance)
			{
				closestAllyDistance = allyDistance;
			}
		}

		// If an ally is closer to the enemy, compute the penaltyMultiplier
		if (closestAllyDistance < distance) 
		{
			penaltyMultiplier = closestAllyDistance / distance; // Ratio of distances
		}

		auto turnsToReach = (distance - 1) / speed + 1;
		const BattleHexArray & hexes = enemy->getAttackableHexes(activeStack);
		auto enemySpeed = enemy->getMovementRange();
		auto speedRatio = speed / static_cast<float>(enemySpeed);
		auto multiplier = (speedRatio > 1 ? 1 : speedRatio) * penaltyMultiplier;

		for(auto & hex : hexes)
		{
			if (!dists.isReachable(hex))
				continue;
			// FIXME: provide distance info for Jousting bonus
			auto bai = BattleAttackInfo(activeStack, enemy, 0, cb->battleCanShoot(activeStack));
			auto attack = AttackPossibility::evaluate(bai, hex, damageCache, hb);

			attack.shootersBlockedDmg = 0; // we do not want to count on it, it is not for sure

			auto score = measureMovementExchange(timing, [&]()
			{
				return calculateExchange(attack, turnsToReach, targets, damageCache, hb, {},
					timing ? &timing->findMoveExchangeDetails : nullptr);
			});

			score.enemyDamageReduce *= multiplier;

#if BATTLE_TRACE_LEVEL >= 1
			logAi->trace("Multiplier: %f, turns: %d, current score %f, new score %f", multiplier, turnsToReach, result.score, scoreValue(score));
#endif

			if(result.score < scoreValue(score)
				|| (result.turnsToReach > turnsToReach && vstd::isAlmostEqual(result.score, scoreValue(score))))
			{
				result.score = scoreValue(score);
				result.positions.clear();

#if BATTLE_TRACE_LEVEL >= 1
				logAi->trace("New high score");
#endif

					BattleHex enemyHex = hex;
					while(!flying
						&& (dists.distances[enemyHex.toInt()] > speed || !dists.isReachable(enemyHex))
						&& dists.predecessors.at(enemyHex.toInt()).isValid())
					{
						enemyHex = dists.predecessors.at(enemyHex.toInt());

						if(dists.accessibility[enemyHex.toInt()] == EAccessibility::ALIVE_STACK)
						{
							auto defenderToBypass = hb->battleGetUnitByPos(enemyHex);
							assert(defenderToBypass != nullptr);
							auto attackHex = dists.predecessors[enemyHex.toInt()];
							
							if(defenderToBypass && dists.isReachable(attackHex) &&
							   defenderToBypass != enemy &&
							   vstd::contains(defenderToBypass->getAttackableHexes(activeStack), attackHex))
							{
#if BATTLE_TRACE_LEVEL >= 1
								logAi->trace("Found target to bypass at %d", enemyHex.toInt());
#endif
								
								auto baiBypass = BattleAttackInfo(activeStack, defenderToBypass, 0, cb->battleCanShoot(activeStack));
								auto attackBypass = AttackPossibility::evaluate(baiBypass, attackHex, damageCache, hb);

								auto adjacentStacks = getAdjacentUnits(enemy);

								adjacentStacks.push_back(defenderToBypass);
								vstd::removeDuplicates(adjacentStacks);

								auto bypassScore = measureMovementExchange(timing, [&]()
								{
									return calculateExchange(
										attackBypass,
										dists.distances[attackHex.toInt()],
										targets,
										damageCache,
										hb,
										adjacentStacks,
										timing ? &timing->findMoveExchangeDetails : nullptr);
								});

								if(scoreValue(bypassScore) > result.score)
								{
									result.score = scoreValue(bypassScore);

#if BATTLE_TRACE_LEVEL >= 1
									logAi->trace("New high score after bypass %f", scoreValue(bypassScore));
#endif
								}
							}
						}
					}
					if(!dists.isReachable(enemyHex))
						continue;

				result.positions.insert(enemyHex);
				result.cachedAttack = attack;
				result.turnsToReach = turnsToReach;
			}
		}
	}

	return result;
}

battle::Units BattleExchangeEvaluator::getAdjacentUnits(const battle::Unit * blockerUnit) const
{
	std::queue<const battle::Unit *> queue;
	battle::Units checkedStacks;

	queue.push(blockerUnit);

	while(!queue.empty())
	{
		auto stack = queue.front();

		queue.pop();
		checkedStacks.push_back(stack);

		auto const & hexes = stack->getSurroundingHexes();
		for(const auto & hex : hexes)
		{
			auto neighbour = cb->battleGetUnitByPos(hex);

			if(neighbour && neighbour->unitSide() == stack->unitSide() && !vstd::contains(checkedStacks, neighbour))
			{
				queue.push(neighbour);
				checkedStacks.push_back(neighbour);
			}
		}
	}

	return checkedStacks;
}

ReachabilityData BattleExchangeEvaluator::getExchangeUnits(
	const AttackPossibility & ap,
	uint8_t turn,
	PotentialTargets & targets,
	std::shared_ptr<HypotheticBattle> hb,
	const battle::Units & additionalUnits) const
{
	ReachabilityData result;

	auto hexes = ap.attack.defender->getSurroundingHexes();

	if(!ap.attack.shooting) 
		hexes.insert(ap.from);

	battle::Units allReachableUnits = additionalUnits;
	
	for(const auto & hex : hexes)
	{
		vstd::concatenate(allReachableUnits, getOneTurnReachableUnits(turn, hex));
	}

	if(!ap.attack.attacker->isTurret())
	{
		for(const auto & hex : ap.attack.attacker->getHexes())
		{
			auto unitsReachingAttacker = getOneTurnReachableUnits(turn, hex);
			for(auto unit : unitsReachingAttacker)
			{
				if(unit->unitSide() != ap.attack.attacker->unitSide())
				{
					allReachableUnits.push_back(unit);
					result.enemyUnitsReachingAttacker.insert(unit->unitId());
				}
			}
		}
	}

	vstd::removeDuplicates(allReachableUnits);

	auto copy = allReachableUnits;
	for(auto unit : copy)
	{
		for(auto adjacentUnit : getAdjacentUnits(unit))
		{
			auto unitWithBonuses = hb->battleGetUnitByID(adjacentUnit->unitId());

			if(vstd::contains(targets.unreachableEnemies, adjacentUnit)
				&& !vstd::contains(allReachableUnits, unitWithBonuses))
			{
				allReachableUnits.push_back(unitWithBonuses);
			}
		}
	}

	vstd::removeDuplicates(allReachableUnits);

	if(!vstd::contains(allReachableUnits, ap.attack.attacker))
	{
		allReachableUnits.push_back(ap.attack.attacker);
	}

	if(allReachableUnits.size() < 2)
	{
#if BATTLE_TRACE_LEVEL>=1
		logAi->trace("Reachability map contains only %d stacks", allReachableUnits.size());
#endif

		return result;
	}

	for(auto unit : allReachableUnits)
	{
		auto accessible = !unit->canShoot() || vstd::contains(additionalUnits, unit);

		if(!accessible)
		{
			for(const auto & hex : unit->getSurroundingHexes())
			{
				if(ap.attack.defender->coversPos(hex))
				{
					accessible = true;
				}
			}
		}

		if(accessible)
			result.melleeAccessible.push_back(unit);
		else
			result.shooters.push_back(unit);
	}

	for(int turn = 0; turn < turnOrder.size(); turn++)
	{
		for(auto unit : turnOrder[turn])
		{
			if(vstd::contains(allReachableUnits, unit))
				result.units[turn].push_back(unit);
		}

		vstd::erase_if(result.units[turn], [&](const battle::Unit * u) -> bool
			{
				return !hb->battleGetUnitByID(u->unitId())->alive();
			});
	}

	return result;
}

float BattleExchangeEvaluator::evaluateExchange(
	const AttackPossibility & ap,
	uint8_t turn,
	PotentialTargets & targets,
	DamageCache & damageCache,
	std::shared_ptr<HypotheticBattle> hb,
	BattleMovementTiming::ExchangeTiming * timing) const
{
	BattleScore score = calculateExchange(ap, turn, targets, damageCache, hb, {}, timing);

#if BATTLE_TRACE_LEVEL >= 1
	logAi->trace(
		"calculateExchange score +%2f -%2fx%2f = %2f",
		score.enemyDamageReduce,
		score.ourDamageReduce,
		getNegativeEffectMultiplier(),
		scoreValue(score));
#endif

	return scoreValue(score);
}

BattleScore BattleExchangeEvaluator::calculateExchange(
	const AttackPossibility & ap,
	uint8_t turn,
	PotentialTargets & targets,
	DamageCache & damageCache,
	std::shared_ptr<HypotheticBattle> hb,
	const battle::Units & additionalUnits,
	BattleMovementTiming::ExchangeTiming * timing) const
{
	ExchangeTimingScope measurement(timing);
#if BATTLE_TRACE_LEVEL>=1
	logAi->trace("Battle exchange at %d", ap.attack.shooting ? ap.dest.toInt() : ap.from.toInt());
#endif

	if(cb->battleGetMySide() == BattleSide::LEFT_SIDE
		&& cb->battleGetGateState() == EGateState::BLOCKED
		&& ap.attack.defender->coversPos(BattleHex::GATE_BRIDGE))
	{
		return BattleScore(EvaluationResult::INEFFECTIVE_SCORE, 0);
	}

	battle::Units ourStacks;
	battle::Units enemyStacks;

	if(hb->battleGetUnitByID(ap.attack.defender->unitId())->alive())
		enemyStacks.push_back(ap.attack.defender);

	ReachabilityData exchangeUnits = measurement.measure(&BattleMovementTiming::ExchangeTiming::membership, [&]()
	{
		return getExchangeUnits(ap, turn, targets, hb, additionalUnits);
	});

	if(exchangeUnits.units.empty())
	{
		return BattleScore();
	}

	MovementExchangeScope setupMeasurement(measurement.stage(&BattleMovementTiming::ExchangeTiming::projectionSetup));
	auto exchangeBattle = std::make_shared<HypotheticBattle>(env.get(), hb);
	BattleExchangeVariant v;

	ExchangeInitiativeQueue::appendParticipantArmies(*exchangeBattle, ap.attack.attacker,
		exchangeUnits.units, ourStacks, enemyStacks);

	setupMeasurement.finish();
	bool canUseAp = true;

	std::set<uint32_t> blockedShooters;

	int totalTurnsCount = simulationTurnsCount >= turn + turnOrder.size()
		? simulationTurnsCount
		: turn + turnOrder.size();

	ExchangeInitiativeQueue exchangeQueue(exchangeBattle, initiativeStrikeEffects);
	for(int exchangeTurn = 0; exchangeTurn < simulationTurnsCount; exchangeTurn++)
	{
		bool isMovingTurm = exchangeTurn < turn;
		int queueTurn = exchangeTurn >= exchangeUnits.units.size()
			? exchangeUnits.units.size() - 1
			: exchangeTurn;

		exchangeQueue.beginRound(turnOrder.at(queueTurn), exchangeUnits.units.at(queueTurn));
		while(const auto * activeUnit = exchangeQueue.next())
		{
			bool isOur = exchangeBattle->battleMatchOwner(ap.attack.attacker, activeUnit, true);
			battle::Units & attackerQueue = isOur ? ourStacks : enemyStacks;
			battle::Units & oppositeQueue = isOur ? enemyStacks : ourStacks;

			auto attacker = exchangeBattle->getForUpdate(activeUnit->unitId());
			auto shooting = exchangeBattle->battleCanShoot(attacker.get())
				&& !attacker->dreadCavalryState.pending
				&& !vstd::contains(blockedShooters, attacker->unitId());

			if(!attacker->alive())
			{
#if BATTLE_TRACE_LEVEL>=1
				logAi->trace("Attacker is dead");
#endif

				continue;
			}

			// The first AP is the already-active unit's current action. Replaying
			// nextTurn here would clear gifts that were armed before this exchange.
			// Every later queue entry is a genuine activation and may consume a
			// pending Cascade gift.
			const bool initialChosenUnit = canUseAp
				&& activeUnit->unitId() == ap.attack.attacker->unitId()
				&& !(attacker->waiting && attacker->forestAmbush.concealed);
			const bool frozenForfeits = !initialChosenUnit
				&& newHorizonsFrozen::forfeitsNormalActivation(*attacker, BattleUnitTurnReason::TURN_QUEUE);
			if(!initialChosenUnit)
			{
				MovementExchangeScope activationMeasurement(measurement.stage(&BattleMovementTiming::ExchangeTiming::activationStart));
				if(frozenForfeits)
					exchangeBattle->nextTurnPreservingFrozenForfeit(attacker->unitId(), BattleUnitTurnReason::TURN_QUEUE,
						measurement.lifecycleSubsets());
				else
					exchangeBattle->nextTurn(attacker->unitId(), BattleUnitTurnReason::TURN_QUEUE,
						measurement.lifecycleSubsets());
			}
			bool fortuneActivation = true;
			auto finishFortuneActivation = [&]()
			{
				if(fortuneActivation)
				{
					MovementExchangeScope activationMeasurement(measurement.stage(&BattleMovementTiming::ExchangeTiming::activationEnd));
					exchangeBattle->endFortuneActivation();
					exchangeBattle->completeSwiftNormalActivation(attacker->unitId());
					fortuneActivation = false;
				}
			};
			if(frozenForfeits)
			{
				MovementExchangeScope activationMeasurement(measurement.stage(&BattleMovementTiming::ExchangeTiming::activationEnd));
				v.completeFrozenActivation(attacker, isOur, damageCache, exchangeBattle);
				activationMeasurement.finish();
				finishFortuneActivation();
				continue;
			}
			if(attacker->armorerLastStandEndedActivation)
			{
				finishFortuneActivation();
				continue; // No trackAttack application can occur for this actor.
			}

			if(isMovingTurm && !shooting
				&& !vstd::contains(exchangeUnits.enemyUnitsReachingAttacker, attacker->unitId()))
			{
#if BATTLE_TRACE_LEVEL>=1
				logAi->trace("Attacker is moving");
#endif
				finishFortuneActivation();
				continue;
			}

			auto targetUnit = ap.attack.defender;

			if(!isOur || !exchangeBattle->battleGetUnitByID(targetUnit->unitId())->alive())
			{
#if BATTLE_TRACE_LEVEL>=2
				logAi->trace("Best target selector for %s", attacker->getDescription());
#endif
				auto estimateAttack = [&](const battle::Unit * u, std::optional<bool> & readyOverwatch,
					DamageCacheAdmission & admission) -> float
				{
					auto stackWithBonuses = exchangeBattle->getForUpdate(u->unitId());
					const auto score = measurement.measure(&BattleMovementTiming::ExchangeTiming::targetProbes, [&]()
					{
						const BattleTargetProbeTiming probeTiming{
							measurement.stage(&BattleMovementTiming::ExchangeTiming::targetProbeMainDamage),
							measurement.stage(&BattleMovementTiming::ExchangeTiming::targetProbeDpsScore),
							measurement.probeDamageCache(),
							measurement.stage(&BattleMovementTiming::ExchangeTiming::targetProbePreparation)};
						return v.trackAttack(attacker, stackWithBonuses, shooting, isOur,
							damageCache, exchangeBattle, true, true, &readyOverwatch,
							timing ? &probeTiming : nullptr, &admission);
					});

#if BATTLE_TRACE_LEVEL>=2
					logAi->trace("Best target selector %s->%s score = %2f", attacker->getDescription(), stackWithBonuses->getDescription(), score);
#endif

					return score;
				};
				auto selectBestTarget = [&](const battle::Units & candidates) -> const battle::Unit *
				{
					if(candidates.empty())
						return nullptr;

					const battle::Unit * best = candidates.front();
					// maxElementByFun never invokes its comparator for one element.
					if(candidates.size() == 1)
						return best;

					// evaluateOnly keeps this branch's readiness unchanged. End the
					// hint's lifetime before any applied attack, turn or round boundary.
					std::optional<bool> readyOverwatch;
					DamageCacheAdmission admission(*attacker, *exchangeBattle);
					float bestScore = estimateAttack(best, readyOverwatch, admission);
					for(size_t index = 1; index < candidates.size(); ++index)
					{
						const auto * candidate = candidates[index];
						const float candidateScore = estimateAttack(candidate, readyOverwatch, admission);
						if(candidateScore > bestScore)
						{
							best = candidate;
							bestScore = candidateScore;
						}
					}
					return best;
				};

				auto unitsInOppositeQueueExceptInaccessible = oppositeQueue;

				vstd::erase_if(unitsInOppositeQueueExceptInaccessible, [&](const battle::Unit * u)->bool
					{
						return vstd::contains(exchangeUnits.shooters, u);
					});

				if(!isOur
					&& exchangeTurn == 0
					&& exchangeUnits.units.at(exchangeTurn).at(0)->unitId() != ap.attack.attacker->unitId()
					&& !vstd::contains(exchangeUnits.enemyUnitsReachingAttacker, attacker->unitId()))
				{
					vstd::erase_if(unitsInOppositeQueueExceptInaccessible, [&](const battle::Unit * u) -> bool
						{
							return u->unitId() == ap.attack.attacker->unitId();
						});
				}

				if(!unitsInOppositeQueueExceptInaccessible.empty())
				{
					targetUnit = selectBestTarget(unitsInOppositeQueueExceptInaccessible);
				}
				else
				{
					auto reachable = exchangeBattle->battleGetUnitsIf([this, &exchangeBattle, &attacker](const battle::Unit * u) -> bool
						{
							if(u->unitSide() == attacker->unitSide())
								return false;

							if(!exchangeBattle->getForUpdate(u->unitId())->alive())
								return false;

							if(!u->getPosition().isValid())
								return false; // e.g. tower shooters

							const auto & reachableUnits = getOneTurnReachableUnits(0, u->getPosition());

							return vstd::contains_if(reachableUnits, [&attacker](const battle::Unit * other) -> bool
								{
									return attacker->unitId() == other->unitId();
								});
						});

					if(!reachable.empty())
					{
						targetUnit = selectBestTarget(reachable);
					}
					else
					{
#if BATTLE_TRACE_LEVEL>=1
						logAi->trace("Battle queue is empty and no reachable enemy.");
#endif
						finishFortuneActivation();
						continue;
					}
				}
			}

			auto defender = exchangeBattle->getForUpdate(targetUnit->unitId());
			const int totalAttacks = AttackPossibility::getAttackCount(*attacker, shooting, *exchangeBattle);

			if(canUseAp && activeUnit->unitId() == ap.attack.attacker->unitId()
				&& targetUnit->unitId() == ap.attack.defender->unitId())
			{
				measurement.measure(&BattleMovementTiming::ExchangeTiming::appliedAttacks, [&]()
				{
					return v.trackAttack(ap, exchangeBattle, damageCache);
				});
			}
			else
			{
				bool manaLeechSequenceSpent = false;
				bool primaryMeleePerformed = false;
				std::vector<petrifyingBloodAI::Pending> bloodSequence;
				for(int i = 0; i < totalAttacks; i++)
				{
					BattleExchangeAttackResult actionResult;
					measurement.measure(&BattleMovementTiming::ExchangeTiming::appliedAttacks, [&]()
					{
						return v.trackAttack(attacker, defender, shooting, isOur, damageCache, exchangeBattle,
							false, i == 0, nullptr, nullptr, nullptr, &actionResult, &manaLeechSequenceSpent, &bloodSequence);
					});

					primaryMeleePerformed |= actionResult.primaryMeleePerformed;
					if(actionResult.completedAction || !attacker->alive() || !defender->alive())
						break;
				}
				v.finishPetrifyingBlood(exchangeBattle, *attacker, bloodSequence, isOur, damageCache);
				meleeSpecialtyAI::complete(*exchangeBattle, attacker->unitId(), primaryMeleePerformed);
			}

			v.trackDreadContinuation(exchangeBattle, attacker->unitId(), damageCache, isOur);
			if(!shooting)
				blockedShooters.insert(defender->unitId());
			// Only the applied attack is a completed action; speculative
			// movement/no-target exits above must not run affliction ticks.
			if(!attacker->isTimeStopped() && attacker->pursuitMovementRemaining == 0
				&& !exchangeBattle->battleHasPendingRangedFollowUp(attacker.get()))
			{
				MovementExchangeScope activationMeasurement(measurement.stage(&BattleMovementTiming::ExchangeTiming::activationEnd));
				attacker->removeUnitBonus(CSelector([](const Bonus * bonus)
				{
					return Bonus::UntilActivationEnds(bonus) && !newHorizonsOffense::isNoQuarterMoralePenalty(bonus);
				}));
				v.completePlagueActivation(attacker->unitId(), exchangeBattle->battleGetOwner(attacker.get()),
					isOur, damageCache, exchangeBattle);
			}
			finishFortuneActivation();

			canUseAp = false;

			vstd::erase_if(attackerQueue, [&](const battle::Unit * u) -> bool
				{
					return !exchangeBattle->battleGetUnitByID(u->unitId())->alive();
				});

			vstd::erase_if(oppositeQueue, [&](const battle::Unit * u) -> bool
				{
					return !exchangeBattle->battleGetUnitByID(u->unitId())->alive();
				});
		}

		measurement.measure(&BattleMovementTiming::ExchangeTiming::roundAdvance, [&]()
		{
			exchangeBattle->nextRound(measurement.lifecycleSubsets());
		});
	}

	auto score = v.getScore();

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

bool BattleExchangeEvaluator::canBeHitThisTurn(const AttackPossibility & ap)
{
	for(auto pos : ap.attack.attacker->getSurroundingHexes())
	{
		for(auto u : getOneTurnReachableUnits(0, pos))
		{
			if(u->unitSide() != ap.attack.attacker->unitSide())
			{
				return true;
			}
		}
	}

	return false;
}

void ReachabilityMapCache::update(const std::vector<battle::Units> & turnOrder, std::shared_ptr<HypotheticBattle> hb)
{
	for(auto turn : turnOrder)
	{
		for(auto u : turn)
		{
			if(!vstd::contains(unitReachabilityMap, u->unitId()))
			{
				unitReachabilityMap[u->unitId()] = hb->getReachability(u);
			}
		}
	}

	hexReachabilityPerTurn.clear();
}

void BattleExchangeEvaluator::updateReachabilityMap(std::shared_ptr<HypotheticBattle> hb)
{
	const int TURN_DEPTH = 2;

	turnOrder.clear();

	hb->battleGetTurnOrder(turnOrder, std::numeric_limits<int>::max(), TURN_DEPTH);
	reachabilityMap.update(turnOrder, hb);
	initiativeStrikeEffects = newHorizonsOrdinaryStrike::mayChangeInitiative(*hb);
}

const battle::Units & ReachabilityMapCache::getOneTurnReachableUnits(std::shared_ptr<CBattleInfoCallback> cb, std::shared_ptr<Environment> env, const std::vector<battle::Units> & turnOrder, uint8_t turn, const BattleHex & hex)
{
	auto & turnData = hexReachabilityPerTurn[turn];

	if (!turnData.isValid[hex.toInt()])
	{
		turnData.hexes[hex.toInt()] = computeOneTurnReachableUnits(cb, env, turnOrder, turn, hex);
		turnData.isValid.set(hex.toInt());
	}

	return turnData.hexes[hex.toInt()];
}

battle::Units ReachabilityMapCache::computeOneTurnReachableUnits(std::shared_ptr<CBattleInfoCallback> cb, std::shared_ptr<Environment> env, const std::vector<battle::Units> & turnOrder, uint8_t turn, const BattleHex & hex)
{
	battle::Units result;
	if(turnOrder.empty())
		return result;
	// These queries never advance or mutate the projection. Reuse it only for
	// this hex/turn-order evaluation; no state is retained across cache misses.
	HypotheticBattle turnBattle(env.get(), cb);

	for(int i = 0; i < turnOrder.size(); i++, turn++)
	{
		auto & turnQueue = turnOrder[i];

		for(const battle::Unit * unit : turnQueue)
		{
			if(unit->isTurret())
				continue;

			if(turnBattle.battleCanShoot(unit))
			{
				result.push_back(unit);

				continue;
			}

			auto unitSpeed = unit->getMovementRange(turn);
			auto radius = unitSpeed * (turn + 1);

			auto reachabilityIter = unitReachabilityMap.find(unit->unitId());
			assert(reachabilityIter != unitReachabilityMap.end()); // missing updateReachabilityMap call?

			std::optional<ReachabilityInfo> fallbackReachability;
			const ReachabilityInfo * reachability = nullptr;
			if(reachabilityIter != unitReachabilityMap.end())
				reachability = &reachabilityIter->second;
			else
			{
				fallbackReachability.emplace(turnBattle.getReachability(unit));
				reachability = &*fallbackReachability;
			}
			const ReachabilityInfo & unitReachability = *reachability;

			// Distances may be finite through Passing Lines (or Ghost Walk) even
			// when this occupied hex is not a legal movement destination.
			bool reachable = unitReachability.isReachable(hex)
				&& unitReachability.distances.at(hex.toInt()) <= radius;

			if(!reachable && unitReachability.accessibility[hex.toInt()] == EAccessibility::ALIVE_STACK)
			{
				const battle::Unit * hexStack = turnBattle.battleGetUnitByPos(hex);

				if(hexStack && controlledByDifferentPlayer(turnBattle, unit, hexStack))
				{
					for(const BattleHex & neighbour : hex.getNeighbouringTiles())
					{
						reachable = unitReachability.isReachable(neighbour)
							&& unitReachability.distances.at(neighbour.toInt()) <= radius;

						if(reachable) break;
					}
				}
			}

			if(reachable)
			{
				result.push_back(unit);
			}
		}
	}

	return result;
}

const battle::Units & BattleExchangeEvaluator::getOneTurnReachableUnits(uint8_t turn, const BattleHex & hex) const
{
	return reachabilityMap.getOneTurnReachableUnits(cb, env, turnOrder, turn, hex);
}

// avoid blocking path for stronger stack by weaker stack
bool BattleExchangeEvaluator::checkPositionBlocksOurStacks(const HypotheticBattle & hb, const battle::Unit * activeUnit, const BattleHex & position)
{
	const int BLOCKING_THRESHOLD = 70;
	const int BLOCKING_OWN_ATTACK_PENALTY = 100;
	const int BLOCKING_OWN_MOVE_PENALTY = 1;

	float blockingScore = 0;

	auto activeUnitDamage = activeUnit->getMinDamage(hb.battleCanShoot(activeUnit)) * activeUnit->getCount();

	for(int turn = 0; turn < turnOrder.size(); turn++)
	{
		auto & turnQueue = turnOrder[turn];
		HypotheticBattle turnBattle(env.get(), cb);

		auto unitToUpdate = turnBattle.getForUpdate(activeUnit->unitId());
		unitToUpdate->setPosition(position);

		for(const battle::Unit * unit : turnQueue)
		{
			if(unit->unitId() == unitToUpdate->unitId()
				|| controlledByDifferentPlayer(turnBattle, unit, activeUnit))
				continue;

			auto blockedUnitDamage = unit->getMinDamage(hb.battleCanShoot(unit)) * unit->getCount();
			float ratio = blockedUnitDamage / (float)(blockedUnitDamage + activeUnitDamage + 0.01);

			auto unitReachability = turnBattle.getReachability(unit);
			auto unitSpeed = unit->getMovementRange(turn); // Cached value, to avoid performance hit

			for(BattleHex hex = BattleHex::TOP_LEFT; hex.isValid(); ++hex)
			{
				bool enemyUnit = false;
				// A transit-occupied hex must not be scored as a legal endpoint.
				bool reachable = unitReachability.isReachable(hex)
					&& unitReachability.distances.at(hex.toInt()) <= unitSpeed;

				if(!reachable && unitReachability.accessibility[hex.toInt()] == EAccessibility::ALIVE_STACK)
				{
					const battle::Unit * hexStack = turnBattle.battleGetUnitByPos(hex);

					if(hexStack && controlledByDifferentPlayer(turnBattle, unit, hexStack))
					{
						enemyUnit = true;
						for(const BattleHex & neighbour : hex.getNeighbouringTiles())
						{
							reachable = unitReachability.isReachable(neighbour)
								&& unitReachability.distances.at(neighbour.toInt()) <= unitSpeed;

							if(reachable) break;
						}
					}
				}

				if(!reachable)
				{
					auto reachableUnits = getOneTurnReachableUnits(0, hex);
					if (std::count(reachableUnits.begin(), reachableUnits.end(), unit) > 1)
						blockingScore += ratio * (enemyUnit ? BLOCKING_OWN_ATTACK_PENALTY : BLOCKING_OWN_MOVE_PENALTY);
				}
			}
		}
	}

#if BATTLE_TRACE_LEVEL>=1
	logAi->trace("Position %d, blocking score %f", position.toInt(), blockingScore);
#endif

	return blockingScore > BLOCKING_THRESHOLD;
}
