/*
 * OriginalMeleeSequence.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "OriginalMeleeSequence.h"
#include "ProjectedMeleeHit.h"
#include "ProjectedMeleeExpiry.h"
#include "MeleeSequenceSupport.h"
#include "HypotheticBattle.h"
#include "InitialExportBattleSubject.h"
#include "AttackPossibility.h"
#include "../../lib/battle/OrdinaryMeleeRetaliation.h"

namespace
{
struct OriginalLoop
{
	std::vector<Bonus> actorSequenceExpiry;
	std::vector<Bonus> targetSequenceExpiry;
	bool longWeapon;
	bool longReach;
};

std::vector<Bonus> sequenceExpiry(const battle::Unit & unit)
{
	std::vector<Bonus> result;
	const auto bonuses = unit.getAllBonuses(Bonus::untilAfterAttackSequence);
	for(const auto & bonus : *bonuses)
		result.push_back(*bonus);
	return result;
}

bool addStrike(heroOrderValue::OriginalMeleeSequence & result, ProjectedMeleeExpiry & expiry,
	const projectedMeleeHit::Request & request)
{
	const auto * actor = result.branch->battleGetUnitByID(request.actorId);
	const auto * victim = result.branch->battleGetUnitByID(request.victimId);
	if(!actor || !victim)
	{
		result.stop = heroOrderValue::MeleeSequenceStop::UNSUPPORTED_REACTION_POLICY;
		return false;
	}
	if(const auto reason = meleeSequenceSupport::unavailable(*result.branch, *actor, *victim, request.selectedTargetHex))
	{
		result.stop = *reason;
		return false;
	}
	const auto hit = projectedMeleeHit::apply(*result.branch, expiry, request, result.stop);
	if(!hit)
		return false;
	result.hits.push_back(*hit);
	return true;
}

bool bracePrefix(heroOrderValue::OriginalMeleeSequence & result, ProjectedMeleeExpiry & expiry,
	const heroOrderValue::BraceStageEntry & entry)
{
	using namespace heroOrderValue;
	const auto forecast = forecastBraceAtStage(*result.branch, entry);
	if(forecast.coverage == BraceStageCoverage::NOT_TRIGGERED)
		return true;
	if(forecast.coverage != BraceStageCoverage::CONDITIONAL_PRIMARY_HIT || !forecast.event || !forecast.primary
		|| forecast.event->attackerId != entry.bracedDefenderId || forecast.event->victimId != entry.incomingAttackerId
		|| forecast.event->victimPosition != result.originalAction.attackFrom
		|| forecast.event->voluntaryMovementDistance != entry.voluntaryMovementDistance)
	{
		result.stop = MeleeSequenceStop::UNSUPPORTED_REACTION_POLICY;
		return false;
	}
	return addStrike(result, expiry, {MeleeStrikeKind::BRACE_PRIMARY, entry.bracedDefenderId, entry.incomingAttackerId,
		result.originalAction.attackFrom, 0, 0, forecast.primary->expectedRequestedDamage});
}

void finishSequence(heroOrderValue::OriginalMeleeSequence & result, ProjectedMeleeExpiry & expiry,
	const OriginalLoop & original)
{
	// No supported strike creates an UNTIL_AFTER_ATTACK_SEQUENCE bonus; copies
	// identify exactly the before-sequence records, never an eager end selector.
	expiry.expireSelected(result.originalAction.incomingAttackerId, original.actorSequenceExpiry);
	expiry.expireSelected(result.originalAction.requestedDefenderId, original.targetSequenceExpiry);
	result.stop = heroOrderValue::MeleeSequenceStop::SUPPORTED_SEQUENCE_FINISHED;
}

bool incomingLoop(heroOrderValue::OriginalMeleeSequence & result, ProjectedMeleeExpiry & expiry, const OriginalLoop & original,
	const heroOrderValue::BraceStageEntry & entry)
{
	using namespace heroOrderValue;
	for(int index = 0; index < result.originalAttackCount; ++index)
	{
		const auto * actor = result.branch->battleGetUnitByID(entry.incomingAttackerId);
		const auto * victim = result.branch->battleGetUnitByID(entry.bracedDefenderId);
		if(!actor->alive() || !victim->alive() || actor->acquireState()->armorerLastStandEndedActivation)
			break;
		if(!addStrike(result, expiry, {MeleeStrikeKind::INCOMING_PRIMARY, actor->unitId(), victim->unitId(),
			result.originalAction.targetHex, index, index == 0 ? entry.voluntaryMovementDistance : 0}))
			return false;
		++result.incomingBlows;
		actor = result.branch->battleGetUnitByID(entry.incomingAttackerId);
		victim = result.branch->battleGetUnitByID(entry.bracedDefenderId);
		if(actor->acquireState()->armorerLastStandEndedActivation)
			break;
		const bool blocked = actor->hasBonusOfType(BonusType::BLOCKS_RETALIATION);
		const bool shroud = result.branch->battleShroudDeniesRetaliation(BattleAttackInfo(actor, victim, entry.voluntaryMovementDistance, false));
		if(battle::canRetaliateAfterOrdinaryMelee(*actor, *victim,
			{blocked, original.longWeapon, original.longReach, index, false, shroud, false}))
			if(!addStrike(result, expiry, {MeleeStrikeKind::ORDINARY_RETALIATION, victim->unitId(), actor->unitId(), actor->getPosition(), 0, 0}))
				return false;
	}
	return true;
}
}

namespace heroOrderValue
{
namespace
{
OriginalMeleeSequence projectSequence(const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & beforeMovement, const BraceMeleeAction & action,
	bool explicitRecipientHistory)
{
	OriginalMeleeSequence result{action};
	if(!environment || !beforeMovement || !beforeMovement->getBattle())
		return result;
	if(const auto policy = meleeSequenceSupport::unavailablePolicies(*beforeMovement))
	{
		result.stop = *policy;
		return result;
	}
	const auto prepared = prepareBraceStage(environment, beforeMovement, action);
	if(!prepared)
		return result;
	result.branch = prepared->branch;
	result.incomingEntry = prepared->entry;
	const auto * actor = result.branch->battleGetUnitByID(action.incomingAttackerId);
	const auto * victim = result.branch->battleGetUnitByID(action.requestedDefenderId);
	if(const auto reason = meleeSequenceSupport::unavailable(*result.branch, *actor, *victim, action.targetHex))
	{
		result.stop = *reason;
		return result;
	}
	result.originalAttackCount = AttackPossibility::getAttackCount(*actor, false, *result.branch);
	if(result.originalAttackCount <= 0 || result.originalAttackCount > 32)
		return result; // Explicit bounded forecast scope, not a legal-action cap.
	const OriginalLoop original{sequenceExpiry(*actor), sequenceExpiry(*victim),
		result.branch->isLongWeaponAttack(actor, victim), !result.branch->isMeleeAttackPossible(actor, victim)
			&& result.branch->isMeleeAttackPossibleWithLongReach(actor, victim)};
	ProjectedMeleeExpiry expiry;
	if(!(explicitRecipientHistory ? expiry.captureRecipientLayers(*result.branch) : expiry.capture(*result.branch)))
	{
		result.stop = MeleeSequenceStop::UNSUPPORTED_REACTION_POLICY;
		return result; // Missing record ownership never becomes successful expiry.
	}
	if(!bracePrefix(result, expiry, prepared->entry) || !incomingLoop(result, expiry, original, prepared->entry))
		return result;
	finishSequence(result, expiry, original);
	return result;
}
}

OriginalMeleeSequence projectOriginalMeleeSequence(const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & beforeMovement, const BraceMeleeAction & action)
{
	return projectSequence(environment, beforeMovement, action, false);
}

OriginalMeleeSequence projectRecipientMeleeSequence(const Environment * environment,
	const std::shared_ptr<InitialExportBattleSubject> & beforeMovement, const BraceMeleeAction & action)
{
	return projectSequence(environment, beforeMovement, action, true);
}
}
