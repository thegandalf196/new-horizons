/*
 * ProjectedMeleeHit.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "SylvanTacticsProjection.h"
#include "../../lib/battle/NewHorizonsForestAmbush.h"
#include "ProjectedMeleeHit.h"
#include "../../lib/battle/NewHorizonsSoulHarvest.h"
#include "ProjectedMeleeExpiry.h"
#include "ProjectedTargetHit.h"
#include "HypotheticBattle.h"
#include "InitialExportBattleSubject.h"
#include "StackWithBonuses.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/NewHorizonsArmorer.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"

namespace projectedMeleeHit
{
std::optional<heroOrderValue::MeleeHitReceipt> apply(HypotheticBattle & battle, ProjectedMeleeExpiry & expiry,
	const Request & request, heroOrderValue::MeleeSequenceStop & unavailableReason)
{
	using namespace heroOrderValue;
	unavailableReason = MeleeSequenceStop::UNSUPPORTED_REACTION_POLICY;
	const auto * actorBefore = battle.battleGetUnitByID(request.actorId);
	const auto * victimBefore = battle.battleGetUnitByID(request.victimId);
	if(!actorBefore || !victimBefore || !actorBefore->alive() || !victimBefore->alive()
		|| !victimBefore->coversPos(request.selectedTargetHex))
		return {};
	auto actor = battle.getForUpdate(request.actorId);
	auto victim = battle.getForUpdate(request.victimId);
	const auto ownedSubject = battle.initialExportSubject();
	JsonNode originalVictimState;
	if(ownedSubject)
	{
		ownedSubject->synchronizeValues(battle);
		originalVictimState = ownedSubject->world().snapshotAt(ownedSubject->occurrence(request.victimId));
	}
	const bool brace = request.kind == MeleeStrikeKind::BRACE_PRIMARY;
	const bool retaliation = request.kind == MeleeStrikeKind::ORDINARY_RETALIATION;
	BattleAttackInfo strike(actor.get(), victim.get(), request.distanceArgument, false);
	strike.retaliation = retaliation;
	strike.ordinaryAttackIndex = request.attackIndex;
	strike.bracePreemptive = brace;
	strike.attackerPos = actor->getPosition();
	strike.defenderPos = victim->getPosition();
	if(!brace && !retaliation && request.attackIndex == 0
		&& battle.battleCanUsePerfectMoment(actor.get(), victim.get(), false))
		return {}; // Original first-strike guarantee context needs its shared owner.
	const auto outcome = battle.captureFortuneStrikeOutcome(strike);
	if(outcome == ProjectedLuckOutcome::UNKNOWN)
	{
		unavailableReason = MeleeSequenceStop::UNKNOWN_LUCK_PENDING;
		return {};
	}
	const auto side = battle.playerToSide(battle.battleGetActionController(actor.get()));
	const bool perfect = battle.battleCanTriggerPerfectFortune(strike);
	const auto requested = request.capturedBraceRequest ? *request.capturedBraceRequest : battle.battleExpectedLuckDamage(strike);
	const auto actorHealth = actor->getAvailableHealth(), beforeHealth = victim->getAvailableHealth();
	const projectedTargetHit::Context context{victim->unitId(),
		newHorizonsArmorer::isEligiblePhysicalAttack(actor.get(), true, false), retaliation, brace || retaliation};
	const auto prepared = projectedTargetHit::prepare(battle, *victim, requested, context);
	const bool bastion = battle.battleHasBastionProtection(victim.get());
	auto damage = prepared.damageToApply;
	const bool harvestEligible = newHorizonsSoulHarvest::qualifies(battle, *actor, *victim, strike.physicalDamage);
	const bool victimMayRebirth = battle.hasReadyNativeRebirth(victim.get());
	const int32_t victimCountBefore = harvestEligible ? victim->getCount() : 0;
	sylvanTacticsAI::StrikeProjection tactics(&battle);
	tactics.damage(battle, strike, *victim, damage, battle::DamageProvenance::PHYSICAL_CREATURE);
	tactics.finish(actor->unitId());
	if(harvestEligible)
		newHorizonsSoulHarvest::grant(*actor, newHorizonsSoulHarvest::casualtiesAfterHit(
			*victim, victimCountBefore, victimMayRebirth));
	battle.projectFortuneStrike(strike, {{victim->unitId(), damage}}, actor.get(), !victim->alive(),
		outcome, true, perfect, side, side, newHorizonsCombatSkills::isPhysicalCreatureLuckAttack(actor.get(), true));
	actor->afterAttack(false, retaliation, true); // Brace is reactive but never ordinary retaliation consumption.
	if(ownedSubject)
	{
		ownedSubject->recordState(battle, actor->unitId(), 0);
		ownedSubject->recordState(battle, victim->unitId(), -damage, &originalVictimState);
	}
	// Native attackerChanges captures resources BEFORE the victim update can
	// expire/unpropagate an aura that the attacker's resource owner still reads.
	if(!ownedSubject && victim->getAvailableHealth() < beforeHealth)
		expiry.expire(victim->unitId(), Bonus::UntilBeingAttacked);
	battle.recordBloodrageTransition(victim, true);
	expiry.expire(actor->unitId(), Bonus::UntilAttack);
	if(!retaliation)
		expiry.expire(actor->unitId(), Bonus::UntilOwnAttack);
	assert(prepared.context.targetId == victim->unitId());
	projectedTargetHit::finishAfterDefiant(battle, *victim, prepared);
	if(bastion)
		victim->armorerBastionRound = battle.battleGetRound();
	return MeleeHitReceipt{request.kind, actor->unitId(), victim->unitId(), actor->getPosition(),
		victim->getPosition(), request.selectedTargetHex, battle.battleGetRound(), battle.getActiveStackID(),
		request.attackIndex, request.distanceArgument,
		actorHealth, beforeHealth, requested, damage, victim->getAvailableHealth(),
		battle::DamageProvenance::PHYSICAL_CREATURE, retaliation, brace || retaliation, prepared.lastStandTriggered};
}
}
