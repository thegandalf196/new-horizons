/*
 * BattleExchangeFutureAttack.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ToxicStingValue.h"
#include "../../lib/battle/NewHorizonsGnollHarriers.h"
#include "BattleExchangeVariant.h"
#include "SpellTargetsEvaluator.h"
#include "../../lib/battle/NewHorizonsChargeSpecialties.h"
#include "../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsCoordinatedMarks.h"

namespace
{
bool hasReadyOverwatch(const std::shared_ptr<HypotheticBattle> & battle,
	bool evaluateOnly, std::optional<bool> * cached)
{
	// Only this existence query is reusable within an immutable probe batch.
	if(evaluateOnly && cached && cached->has_value())
		return cached->value();
	const bool ready = std::ranges::any_of(battle->battleAliveUnits(), [&](const battle::Unit * unit)
		{ return battle->battleOverwatchReady(unit); });
	if(evaluateOnly && cached)
		*cached = ready;
	return ready;
}

bool hasReadyBattlecraft(const HypotheticBattle & battle,
	const battle::Unit * attacker, const battle::Unit * defender)
{
	const BattleAttackInfo incoming(attacker, defender, 0, false);
	return defender && newHorizonsCombatSkills::isPhysicalCreatureAttack(attacker, incoming.physicalDamage)
		&& battle.battleMatchOwner(attacker, defender, false)
		&& newHorizonsBattlecraft::preemptiveStrikeDamagePercent(
			battle.battleGetOwnerHero(defender), defender, battle.battleGetRound()) > 0;
}

bool hasBattlecraftReceipt(const AttackPossibility & projected, const HypotheticBattle & battle)
{
	const auto round = battle.battleGetRound();
	return std::ranges::any_of(projected.affectedUnits, [&](const auto & unit)
	{
		const auto * before = battle.battleGetUnitByID(unit->unitId());
		return unit->battlecraftPreemptiveStrikeRound == round && before
			&& before->acquireState()->battlecraftPreemptiveStrikeRound != round;
	});
}

BattleHex currentApproach(const std::shared_ptr<HypotheticBattle> & battle,
	const battle::Unit * attacker, const battle::Unit * defender,
	const ReachabilityInfo & reachable)
{
	const auto available = battle->battleGetAvailableHexes(reachable, attacker, true);
	BattleHex approach;
	int distance = GameConstants::BFIELD_SIZE;
	// Preserve the existing nearest-reachable policy and first-in-order ties.
	// Do not choose a longer route to manufacture a Charge/Brace trigger.
	for(const auto hex : defender->getAttackableHexes(attacker))
		if(available.contains(hex) && reachable.distances[hex.toInt()] < distance
			&& battle->isMeleeAttackPossibleWithLongReach(attacker, defender, hex))
		{
			approach = hex;
			distance = reachable.distances[hex.toInt()];
		}
	return approach;
}
}

std::optional<float> BattleExchangeVariant::trackFutureMovementReaction(
	const std::shared_ptr<StackWithBonuses> & attacker,
	const std::shared_ptr<StackWithBonuses> & defender,
	bool shooting, bool isOurAttack, DamageCache & damageCache,
	const std::shared_ptr<HypotheticBattle> & hb, bool evaluateOnly,
	bool allowRetaliation, std::optional<bool> * readyOverwatch)
{
	if(!defender || shooting || !allowRetaliation
		|| attacker->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE)
		|| attacker->hasBonusOfType(BonusType::CONFUSION_PENDING))
		return std::nullopt;
	// In the production future-actor loop false denotes a subsequent single
	// ordinary hit, not BattleAttackInfo::retaliation. Its historical direct-call
	// contract has no move prefix/counter; do not replace it with a full AP action.
	const auto * resolvedDefender = hb->battleResolveHeroOrderTarget(attacker.get(), defender.get(), false);
	// Eligibility admits the existing path query; AP owns ordered reactions.
	// Protect can expose the requested target again on a later primary blow.
	const bool readyBattlecraft = hasReadyBattlecraft(*hb, attacker.get(), resolvedDefender)
		|| (resolvedDefender != defender.get()
			&& hasReadyBattlecraft(*hb, attacker.get(), defender.get()));
	const bool readyHistory = newHorizonsGnollHarriers::trackingRequired(*hb);
	const bool readySting = toxicStingAI::requiresPreview(*hb, *attacker, *defender, *resolvedDefender);
	const bool readyCharge = newHorizonsChargeSpecialties::requiresChargeMovementPreview(*hb, *attacker, *resolvedDefender)
		|| (resolvedDefender != defender.get()
			&& newHorizonsChargeSpecialties::requiresChargeMovementPreview(*hb, *attacker, *defender));
	const bool readyReturn = newHorizonsCoordinatedMarks::canMarkReturn(*hb, *attacker)
		&& attacker->alive() && !attacker->isGhost() && !attacker->isTimeStopped()
		&& !attacker->hasBonusOfType(BonusType::NOT_ACTIVE) && !attacker->hasBonusOfType(BonusType::BIND_EFFECT)
		&& attacker->hasBonusOfType(BonusType::RETURN_AFTER_STRIKE);
	const bool eligibleBrace = hb->battleCanTriggerHeroOrderBrace(
		attacker.get(), resolvedDefender, 3, false, false);
	if(!readySting && !readyHistory && !readyCharge && !readyReturn && !readyBattlecraft && !eligibleBrace && !hasReadyOverwatch(hb, evaluateOnly, readyOverwatch))
		return std::nullopt;
	const auto reachable = hb->getReachability(attacker.get());
	const auto approach = currentApproach(hb, attacker.get(), defender.get(), reachable);
	if(!approach.isAvailable() || (approach == attacker->getPosition() && !readySting && !readyBattlecraft))
		return std::nullopt;
	const int distance = SpellTargetEvaluator::physicalTravelDistance(reachable, approach);
	if(distance < 0)
		return std::nullopt;
	const bool readyBrace = hb->battleCanTriggerHeroOrderBrace(
		attacker.get(), resolvedDefender, distance, false, false);
	if(!readySting && !readyHistory && !readyCharge && !readyReturn && !readyBattlecraft && !readyBrace && !hasReadyOverwatch(hb, evaluateOnly, readyOverwatch))
		return std::nullopt;

	// Every target probe owns a child, including its units/Orders/reaction state.
	// An applied call uses only the already selected target's supplied branch.
	auto branch = evaluateOnly ? std::make_shared<HypotheticBattle>(hb->env, hb) : hb;
	const auto actor = branch->getForUpdate(attacker->unitId());
	const auto target = branch->getForUpdate(defender->unitId());
	BattleAttackInfo incoming(actor.get(), target.get(), distance, false);
	// This existing path was selected as a voluntary approach, never a forced
	// reaction, teleport or later blow. Walking movement carries no flight fact.
	incoming.voluntaryFlightDistance = reachable.params.flying ? distance : 0;
	const auto projected = AttackPossibility::evaluate(incoming, approach, damageCache, branch, false, std::nullopt, nullptr, approach != actor->getPosition(), actor->getPosition());
	const bool hasBraceReceipt = std::ranges::any_of(projected.fortuneStrikes,
		[](const auto & strike) { return strike.bracePreemptive; });
	// A changed round marker is the accepted Battlecraft receipt, even when
	// the hit deals zero damage. Ordinary retaliation metadata is insufficient.
	if(!readySting && !readyHistory && !readyCharge && !readyReturn && projected.overwatchHits.empty() && !hasBraceReceipt && !hasBattlecraftReceipt(projected, *hb))
		return std::nullopt;
	BattleExchangeVariant reactionExchange;
	const auto value = reactionExchange.trackAttack(projected, branch, damageCache);
	if(!evaluateOnly)
	{
		const auto & score = reactionExchange.getScore();
		dpsScore.ourDamageReduce += isOurAttack ? score.ourDamageReduce : score.enemyDamageReduce;
		dpsScore.enemyDamageReduce += isOurAttack ? score.enemyDamageReduce : score.ourDamageReduce;
	}
	return value;
}

std::optional<float> BattleExchangeVariant::trackFutureReaction(
	const std::shared_ptr<StackWithBonuses> & attacker,
	const std::shared_ptr<StackWithBonuses> & defender,
	bool shooting, bool isOurAttack, DamageCache & damageCache,
	const std::shared_ptr<HypotheticBattle> & hb, bool evaluateOnly,
	bool allowRetaliation, std::optional<bool> * readyOverwatch)
{
	if(const auto movement = trackFutureMovementReaction(attacker, defender, shooting,
		isOurAttack, damageCache, hb, evaluateOnly, allowRetaliation, readyOverwatch))
		return movement;
	if(const auto dread = trackDreadAction(attacker, defender, shooting, isOurAttack,
		damageCache, hb, evaluateOnly, allowRetaliation))
		return dread;
	return trackAerialExchange(attacker, defender, shooting, isOurAttack,
		damageCache, hb, evaluateOnly, allowRetaliation);
}
