/*
 * AttackPreviewHitContext.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "AttackPreviewHitContext.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/NewHorizonsArmorer.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsOpportunist.h"

AttackPreviewHitContext::AttackPreviewHitContext(const std::shared_ptr<HypotheticBattle> & branch,
	bool & endedActivation) : fortunePreview(branch), endedActiveActivationByLastStand(endedActivation) {}

newHorizonsArmorer::ArmorerLastStandDamageResult AttackPreviewHitContext::resolveLastStandDamage(
	const BattleAttackInfo & projectedAttack, battle::CUnitState * target, int64_t incomingDamage) const
{
	newHorizonsArmorer::ArmorerLastStandDamageResult result{incomingDamage, false};
	if(!fortunePreview || !target)
		return result;

	const auto side = fortunePreview->playerToSide(fortunePreview->battleGetOwner(target));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return result;
	const bool eligible = newHorizonsArmorer::isEligiblePhysicalAttack(
		projectedAttack.attacker, projectedAttack.physicalDamage,
		projectedAttack.shooting && projectedAttack.attacker
			&& projectedAttack.attacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK))
		&& newHorizonsArmorer::canTriggerLastStand(
			fortunePreview->battleGetOwnerHero(target), target);
	result = newHorizonsArmorer::resolveLastStandDamage(incomingDamage,
		target->getGuardianSpiritHitPoints(), target->getGuardianSpiritRoundsRemaining(),
		target->getAvailableHealth(), eligible, fortunePreview->armorerLastStandUsed(side));
	if(result.triggered)
		fortunePreview->consumeArmorerLastStand(side);
	return result;
}

void AttackPreviewHitContext::applyLastStandDefend(battle::CUnitState * target, bool retaliation) const
{
	if(!fortunePreview || !target)
		return;
	fortunePreview->applyArmorerLastStandDefend(target->unitId());
	const auto activeStackId = fortunePreview->getActiveStackID();
	if(retaliation && activeStackId >= 0
		&& target->unitId() == static_cast<uint32_t>(activeStackId))
	{
		target->armorerLastStandEndedActivation = true;
		endedActiveActivationByLastStand = true;
	}
}

bool AttackPreviewHitContext::captureAndProjectFortuneStrike(const BattleAttackInfo & attack,
	const std::vector<std::pair<uint32_t, int64_t>> & hits,
	battle::CUnitState * attackerState, FortuneStrikeProjection & receipt) const
{
	receipt.luckSerendipitySide = receipt.perfectFortuneSide;
	receipt.luckSerendipityOrdinaryAttack = !attack.secondaryAttack
		&& newHorizonsCombatSkills::isPhysicalCreatureLuckAttack(attack.attacker, attack.physicalDamage);
	if(!fortunePreview || !attack.attacker || !attack.defender)
		return false;
	const auto side = fortunePreview->playerToSide(fortunePreview->battleGetOwner(attack.attacker));
	const bool gamblerAvailable = (side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
		&& fortunePreview->getSylvanLuckState(side).gamblerAttackAvailable();
	const auto fortune = side == BattleSide::ATTACKER || side == BattleSide::DEFENDER
		? fortunePreview->getSylvanLuckState(side) : SylvanLuckState{};
	const bool chainEnabled = fortune.chainOfFortune;
	const bool chainAvailable = chainEnabled && fortune.chainFortuneAvailable(attack.attacker->unitId());
	const auto outcome = receipt.perfectFortune ? ProjectedLuckOutcome::POSITIVE
		: fortunePreview->captureFortuneStrikeOutcome(attack);
	receipt.resolvedLuck = outcome;
	const bool certainNegative = outcome == ProjectedLuckOutcome::NEGATIVE;
	const bool knownPositiveChainTrigger = chainEnabled && outcome == ProjectedLuckOutcome::POSITIVE;
	const bool serendipityEnabled = (receipt.luckSerendipitySide == BattleSide::ATTACKER
		|| receipt.luckSerendipitySide == BattleSide::DEFENDER)
		&& fortunePreview->getLuckSerendipityState(receipt.luckSerendipitySide).enabled;
	const bool opportunistTrigger = !attack.retaliation && !attack.secondaryAttack
		&& attack.physicalDamage && outcome == ProjectedLuckOutcome::POSITIVE
		&& newHorizonsOpportunist::hasPerk(*fortunePreview, attack.attacker);
	if(!receipt.perfectFortune && !certainNegative && !gamblerAvailable && !chainAvailable
		&& !knownPositiveChainTrigger && !serendipityEnabled && !opportunistTrigger)
		return false;

	// Capture Luck before one-strike bonuses are consumed, then update only
	// this detached side history. The selected replay applies positive Luck
	// aftermath once, after its projected hit damage has been replayed.
	// An unresolved eligible Chain strike consumes the gift without arming
	// another one; only the known positive result can arm a pending source.
	fortunePreview->projectFortuneStrike(attack, hits, attackerState, false,
		receipt.resolvedLuck, false, receipt.perfectFortune, receipt.perfectFortuneSide,
		receipt.luckSerendipitySide, receipt.luckSerendipityOrdinaryAttack);
	return true;
}
