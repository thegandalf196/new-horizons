/*
 * BattleExchangeFutureAttack.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleExchangeVariant.h"
#include "SpellTargetsEvaluator.h"

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

std::optional<float> BattleExchangeVariant::trackFutureReaction(
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
	// Eligibility here only admits a path query. The ACTUAL chosen path distance
	// and AP's shared ordered stages decide whether a Brace primary can occur.
	const bool eligibleBrace = hb->battleCanTriggerHeroOrderBrace(
		attacker.get(), resolvedDefender, 3, false, false);
	if(!eligibleBrace && !hasReadyOverwatch(hb, evaluateOnly, readyOverwatch))
		return std::nullopt;
	const auto reachable = hb->getReachability(attacker.get());
	const auto approach = currentApproach(hb, attacker.get(), defender.get(), reachable);
	if(!approach.isAvailable() || approach == attacker->getPosition())
		return std::nullopt;
	const int distance = SpellTargetEvaluator::physicalTravelDistance(reachable, approach);
	if(distance < 0)
		return std::nullopt;
	const bool readyBrace = hb->battleCanTriggerHeroOrderBrace(
		attacker.get(), resolvedDefender, distance, false, false);
	if(!readyBrace && !hasReadyOverwatch(hb, evaluateOnly, readyOverwatch))
		return std::nullopt;

	// Every target probe owns a child, including its units/Orders/reaction state.
	// An applied call uses only the already selected target's supplied branch.
	auto branch = evaluateOnly ? std::make_shared<HypotheticBattle>(hb->env, hb) : hb;
	const auto actor = branch->getForUpdate(attacker->unitId());
	const auto target = branch->getForUpdate(defender->unitId());
	const auto projected = AttackPossibility::evaluate(
		BattleAttackInfo(actor.get(), target.get(), distance, false), approach, damageCache, branch);
	const bool hasBraceReceipt = std::ranges::any_of(projected.fortuneStrikes,
		[](const auto & strike) { return strike.bracePreemptive; });
	if(projected.overwatchHits.empty() && !hasBraceReceipt)
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
