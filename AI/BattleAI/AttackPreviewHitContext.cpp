/*
 * AttackPreviewHitContext.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "DamageCacheMarks.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "AttackPreviewHitContext.h"
#include "ParalyzingGazeProjection.h"
#include "StackWithBonuses.h"
#include "../../lib/battle/NewHorizonsOrdinaryStrike.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/NewHorizonsArmorer.h"
#include "../../lib/battle/NewHorizonsSoulHarvest.h"
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

bool AttackPreviewHitContext::hasSecondChance(const CBattleInfoCallback & state, const battle::Unit * unit, bool physicalDamage)
{
	if(!unit || !state.getBattle()
		|| !newHorizonsCombatSkills::isPhysicalCreatureLuckAttack(unit, physicalDamage))
		return false;
	const auto side = state.playerToSide(state.battleGetOwner(unit));
	return (side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
		&& state.getBattle()->getSylvanLuckState(side).secondChance;
}

bool AttackPreviewHitContext::hasDefiant(const CBattleInfoCallback & state)
{
	return std::ranges::any_of(std::array{BattleSide::ATTACKER, BattleSide::DEFENDER}, [&state](BattleSide side)
	{
		const auto * hero = state.getBattle()->getSideHero(side);
		return hero && hero->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.defiant")
			&& state.getBattle()->getArmorerDefiantState(side).availableAt(state.battleGetRound());
	});
}
bool AttackPreviewHitContext::hasLuckSerendipity(const CBattleInfoCallback & state)
{
	return std::ranges::any_of(std::array{BattleSide::ATTACKER, BattleSide::DEFENDER}, [&state](BattleSide side)
	{
		return state.getBattle()->getLuckSerendipityState(side).enabled;
	});
}

bool AttackPreviewHitContext::hasReadyLastStand(const CBattleInfoCallback & state)
{
	return std::ranges::any_of(state.battleAliveUnits(), [&state](const battle::Unit * target)
	{
		if(!target)
			return false;
		const auto side = state.playerToSide(state.battleGetOwner(target));
		return (side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
			&& !state.getBattle()->armorerLastStandUsed(side)
			&& newHorizonsArmorer::canTriggerLastStand(state.battleGetOwnerHero(target), target);
	});
}

std::pair<newHorizonsArmorer::ArmorerLastStandDamageResult, BattleSide>
AttackPreviewHitContext::resolveUnspentLastStand(const CBattleInfoCallback & state,
	const BattleAttackInfo & attack, const battle::Unit * target, int64_t incomingDamage)
{
	const auto side = target ? state.playerToSide(state.battleGetOwner(target)) : BattleSide::NONE;
	const bool validSide = side == BattleSide::ATTACKER || side == BattleSide::DEFENDER;
	if(!validSide)
		return std::pair{newHorizonsArmorer::ArmorerLastStandDamageResult{incomingDamage, false}, side};

	const bool eligible = newHorizonsArmorer::isEligiblePhysicalAttack(attack.attacker,
		attack.physicalDamage, attack.shooting && attack.attacker
			&& attack.attacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK))
		&& newHorizonsArmorer::canTriggerLastStand(state.battleGetOwnerHero(target), target);
	const auto result = newHorizonsArmorer::resolveLastStandDamage(incomingDamage,
		target->getGuardianSpiritHitPoints(), target->getGuardianSpiritRoundsRemaining(),
		target->getAvailableHealth(), eligible, state.getBattle()->armorerLastStandUsed(side));
	return std::pair{result, side};
}
void AttackPreviewHitContext::applyUnspentLastStand(HypotheticBattle & state, battle::CUnitState * target,
	const newHorizonsArmorer::ArmorerLastStandDamageResult & lastStand, BattleSide side, bool retaliation, bool apply)
{
	if(!apply || !lastStand.triggered || !target)
		return;
	state.consumeArmorerLastStand(side);
	state.applyArmorerLastStandDefend(target->unitId());
	const auto activeStackId = state.getActiveStackID();
	if(retaliation && activeStackId >= 0
		&& target->unitId() == static_cast<uint32_t>(activeStackId))
		target->armorerLastStandEndedActivation = true;
}

bool AttackPreviewHitContext::hasGambler(const CBattleInfoCallback & state)
{
	return state.getBattle()
			&& (state.getBattle()->getSylvanLuckState(BattleSide::ATTACKER).gamblerAttackAvailable()
				|| state.getBattle()->getSylvanLuckState(BattleSide::DEFENDER).gamblerAttackAvailable());
}

bool AttackPreviewHitContext::hasPerfectFortune(const CBattleInfoCallback & state)
{
	return state.getBattle()
			&& (state.getBattle()->getPerfectFortuneState(BattleSide::ATTACKER).enabled
				|| state.getBattle()->getPerfectFortuneState(BattleSide::DEFENDER).enabled);
}

AttackPreviewHitContext::SoulHarvestHit AttackPreviewHitContext::prepareSoulHarvest(const CBattleInfoCallback & state,
	const battle::Unit & actor, const battle::Unit & victim, bool physical)
{
	return {newHorizonsSoulHarvest::qualifies(state, actor, victim, physical),
		victim.canCast() && victim.valOfBonuses(BonusType::REBIRTH) > 0, victim.getCount()};
}

void AttackPreviewHitContext::finishSoulHarvest(const SoulHarvestHit & eligibility,
	battle::CUnitState & actor, const battle::CUnitState & victim)
{
	if(eligibility.eligible)
		newHorizonsSoulHarvest::grant(actor, newHorizonsSoulHarvest::casualtiesAfterHit(
			victim, eligibility.countBefore, eligibility.willRebirth));
}

FortuneStrikeProjection AttackPreviewHitContext::capturePreemptiveStrike(const BattleAttackInfo & attack,
	battle::CUnitState & actor, int64_t requestedDamage, int64_t appliedDamage, bool perfectFortune,
	BattleSide perfectFortuneSide, bool brace, battle::DamageProvenance provenance) const
{
	FortuneStrikeProjection strike;
	strike.perfectFortune = perfectFortune;
	strike.perfectFortuneSide = perfectFortuneSide;
	strike.attackerId = attack.attacker->unitId();
	strike.defenderId = attack.defender->unitId();
	strike.retaliation = attack.retaliation;
	strike.bracePreemptive = brace;
	strike.eligibleForLastStand = newHorizonsArmorer::isEligiblePhysicalAttack(
		attack.attacker, attack.physicalDamage,
		attack.shooting && attack.attacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
	strike.damageProvenance = provenance;
	strike.hits.emplace_back(strike.defenderId, requestedDamage);
	strike.resolvedHits.emplace_back(strike.defenderId, appliedDamage);
	captureAndProjectFortuneStrike(attack, strike.hits,
		&actor, strike);
	return strike;
}

bool AttackPreviewHitContext::canMireGripBeHit(const CBattleInfoCallback & state,
	const battle::Unit * primary, bool longReachAttack,
	const battle::Units & possibleVictims)
{
	const auto qualifies = [&state](const battle::Unit * target)
	{
		return target && target->defended()
			&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(target)
			&& newHorizonsBulwark::hasMireGrip(state.battleGetOwnerHero(target));
	};
	if(!primary)
		return false;
	if(qualifies(primary))
		return true;
	if(longReachAttack)
		return false;
	return std::ranges::any_of(possibleVictims, qualifies);
}

void AttackPreviewHitContext::capturePrimaryFortune(FortuneStrikeProjection & receipt,
	const CBattleInfoCallback & state, const BattleAttackInfo & attack, const battle::Unit & primary)
{
	receipt.perfectFortune = state.battleCanTriggerPerfectFortune(attack);
	receipt.perfectFortuneSide = state.playerToSide(state.battleGetActionController(attack.attacker));
	receipt.attackerId = attack.attacker->unitId();
	receipt.defenderId = primary.unitId();
}
void AttackPreviewHitContext::classifyPrimaryStrike(FortuneStrikeProjection & receipt,
	const BattleAttackInfo & attack, bool perfectMoment, int attackIndex,
	const std::optional<newHorizonsAerialSupremacy::Excursion> & aerial)
{
	receipt.shooting = attack.shooting;
	receipt.retaliation = attack.retaliation;
	receipt.aerialExcursion = aerial;
	receipt.dreadCavalryDamagePercent = attack.dreadCavalryDamagePercent;
	receipt.damageProvenance = battleAIDamageProvenance(attack.attacker, attack.physicalDamage);
	receipt.eligibleForLastStand = newHorizonsArmorer::isEligiblePhysicalAttack(
		attack.attacker, attack.physicalDamage,
		attack.shooting && attack.attacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
	receipt.perfectMoment = perfectMoment;
	// A Master Gunner continuation remains the second shot in its activation.
	receipt.attackIndex = attackIndex;
}

bool AttackPreviewHitContext::anyNightProwler(const battle::Units & units)
{
	return std::ranges::any_of(units, [](const battle::Unit * unit)
	{
		return damageCacheDetail::hasNightProwlerBonus(unit);
	});
}

void AttackPreviewHitContext::applyOrdinaryStrike(HypotheticBattle & branch,
	const BattleAttackInfo & attack, int64_t primaryDamage)
{
	auto current = attack;
	current.attacker = branch.battleGetUnitByID(attack.attacker->unitId());
	current.defender = branch.battleGetUnitByID(attack.defender->unitId());
	const auto effect = newHorizonsOrdinaryStrike::plan(branch, current, primaryDamage);
	if(effect.add.empty())
		return;
	auto target = branch.getForUpdate(effect.target);
	target->removeUnitBonus(effect.remove);
	target->addUnitBonus(effect.add);
}
void AttackPreviewHitContext::applyPrimaryStatuses(const BattleAttackInfo & attack,
	int64_t primaryDamage, bool binding, bool gaze) const
{
	if(!fortunePreview)
		return;
	fortunePreview->projectSerpentBinding(attack.attacker->unitId(), attack.defender->unitId(), primaryDamage, binding);
	paralyzingGazeAI::apply(*fortunePreview, attack.attacker->unitId(), attack.defender->unitId(), primaryDamage, gaze);
	applyOrdinaryStrike(*fortunePreview, attack, primaryDamage);
}

void AttackPreviewHitContext::replayOrdinaryStrikeEffects(HypotheticBattle & destination,
	const AttackPossibility & captured)
{
	if(!captured.effectPreview)
		return;
	std::vector<uint32_t> recipients;
	if(captured.attackerState)
		recipients.push_back(captured.attackerState->unitId());
	for(const auto & affected : captured.affectedUnits)
		if(affected && !vstd::contains(recipients, affected->unitId()))
			recipients.push_back(affected->unitId());
	const CSelector rider(newHorizonsOrdinaryStrike::isStrikeEffect);
	for(const auto id : recipients)
	{
		const auto * final = captured.effectPreview->battleGetUnitByID(id);
		const auto * current = destination.battleGetUnitByID(id);
		if(!final || !current)
			continue;
		const auto saved = final->getUnstackedBonuses(rider);
		if(saved->empty() && !current->hasBonus(rider))
			continue;
		std::vector<Bonus> values;
		for(const auto & bonus : *saved)
			values.push_back(*bonus);
		auto target = destination.getForUpdate(id);
		target->removeUnitBonus(rider);
		target->addUnitBonus(values);
	}
}

void AttackPreviewHitContext::prepareVictimStrike(BattleAttackInfo & attack,
	const CBattleInfoCallback & state, const VictimStrikeInput & input)
{
	attack.secondaryAttack = input.recipient.unitId() != input.primary.unitId();
	attack.ordinaryAttackIndex = attack.retaliation || input.selectedFollowUp ? 0 : input.index;
	attack.relentlessAssaultDamagePercent = input.relentlessAssaultPercent;
	attack.protectIntercepted = input.strike.protectIntercepted
		&& input.recipient.unitId() == input.primary.unitId();
	// The authoritative path spends movement on the first strike and
	// consumes Charge before collateral. Later attacks therefore have no
	// charge distance; collateral retains distance for ordinary jousting,
	// while secondaryAttack prevents it from inheriting the Order.
	if(input.index > 0)
		attack.chargeDistance = 0;
	if(input.strike.perfectMoment || input.strike.perfectFortune)
	{
		attack.luckyStrike = !attack.secondaryAttack || state.getBattle()->getLuckRollRules().affectsAllTargets;
		attack.unluckyStrike = false;
	}
	attack.defenderPos = attack.defender->getPosition();
}
