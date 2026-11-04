/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BlinkPositionValue.h"
#include "HypotheticBattle.h"
#include "AttackPossibility.h"
#include "BattleDamageValue.h"
#include "../../lib/battle/BattleAttackInfo.h"
using battleDamageValue::averageOrderDamage;
namespace
{
/// Values one immediately available direct attack from the chosen landing.
/// This intentionally omits full-battle rollouts and movement/path simulation;
/// it is only a bounded tactical signal for whether Blink changes direct reach.
float blinkDirectAttackValue(const battle::Unit * attacker, const battle::Unit * defender,
	const CBattleInfoCallback & battle, const std::shared_ptr<CBattleInfoCallback> & battleState,
	DamageCache & damageCache, bool includeShooting = true)
{
	if(!attacker || !defender || !attacker->alive() || !defender->alive()
		|| !battle.battleCanAttackUnitAction(attacker, defender))
		return 0.0f;

	const auto availableHealth = std::max<int64_t>(0, defender->getAvailableHealth());
	if(availableHealth <= 0)
		return 0.0f;

	float bestValue = 0.0f;
	const auto valueForAttackMode = [&](bool shooting)
	{
		const auto attackCount = std::max(0, AttackPossibility::getAttackCount(*attacker, shooting, battle));
		if(attackCount <= 0)
			return 0.0f;

		const BattleAttackInfo attack(attacker, defender, 0, shooting);
		const auto perAttackDamage = static_cast<int64_t>(averageOrderDamage(battle.battleEstimateDamage(attack)));
		if(perAttackDamage <= 0)
			return 0.0f;

		const auto totalDamage = std::min(availableHealth, perAttackDamage * static_cast<int64_t>(attackCount));
		return AttackPossibility::calculateDamageReduce(attacker, defender,
			static_cast<uint64_t>(totalDamage), damageCache, battleState);
	};

	if(includeShooting && battle.battleCanShootAction(attacker, defender->getPosition()))
		bestValue = std::max(bestValue, valueForAttackMode(true));
	if(battle.isMeleeAttackPossibleWithLongReach(attacker, defender) || battle.isLongWeaponAttack(attacker, defender))
		bestValue = std::max(bestValue, valueForAttackMode(false));
	return bestValue;
}

/// Signed direct-attack pressure involving a stack at one endpoint. Attacks by
/// the relocated stack are limited to its single best target; attacks by other
/// stacks are accumulated because they represent independent future activations.
float blinkLandingPositionValue(const battle::Unit * original, const BattleHex & landing,
	PlayerColor player, const CBattleInfoCallback & battle,
	const std::shared_ptr<CBattleInfoCallback> & battleState, DamageCache & damageCache)
{
	if(!original || !original->alive() || !landing.isValid())
		return 0.0f;

	auto relocated = original->acquireState();
	if(!relocated)
		return 0.0f;
	relocated->setPosition(landing);

	float bestOutgoing = 0.0f;
	float signedIncoming = 0.0f;
	for(const auto * other : battle.battleGetAllUnits(false))
	{
		if(!other || !other->alive() || other->unitId() == original->unitId()
			|| other->isGhost() || other->isTurret())
			continue;

		bestOutgoing = std::max(bestOutgoing,
			blinkDirectAttackValue(relocated.get(), other, battle, battleState, damageCache));

		// The battle callback resolves a shot's target by occupied hex; a detached
		// relocation is deliberately not installed into that live occupancy map.
		// Keep the positional forecast symmetric by valuing direct incoming melee
		// pressure here while outgoing shots still use the live enemy occupancy.
		const auto incoming = blinkDirectAttackValue(other, relocated.get(), battle, battleState,
			damageCache, false);
		if(incoming > 0.0f)
			signedIncoming += battle.battleGetOwner(other) == player ? incoming : -incoming;
	}

	const float outgoingSign = battle.battleGetOwner(original) == player ? 1.0f : -1.0f;
	return outgoingSign * bestOutgoing + signedIncoming;
}

}
/// Returns the exact expected marginal direct-attack pressure for one Blink
/// target. The shared distribution handles normal uniform destinations and
/// Blinkmaster's two independent draws with replacement; this code consumes no RNG.
float blinkPositionValue::expected(const battle::Unit * target,
	const newHorizonsBlink::Preview & preview, PlayerColor player,
	const CBattleInfoCallback & battle, const std::shared_ptr<CBattleInfoCallback> & battleState,
	DamageCache & damageCache, const Environment * environment, float * inspirationValue,
	const heroSpellProjection::OutcomeValue & outcomeValue)
{
	if(!target || preview.legalDestinations.empty())
		return 0.0f;

	const auto origin = target->getPosition();
	const auto outcomes = newHorizonsBlink::outcomeDistribution(preview, origin);
	if(outcomes.empty() || outcomes.front().totalWeight == 0)
		return 0.0f;

	const auto before = blinkLandingPositionValue(target, origin, player, battle, battleState, damageCache);
	auto coreBoard = outcomeValue ? std::make_shared<HypotheticBattle>(environment, battleState) : nullptr;
	const auto core = outcomeValue ? outcomeValue(coreBoard, {}, false, false) : 0.0f;
	const auto kernel = [&](const std::shared_ptr<HypotheticBattle> & board)
	{
		DamageCache local;
		const auto * unit = board->battleGetUnitByID(target->unitId());
		return blinkLandingPositionValue(unit, unit->getPosition(), player, *board, board, local);
	};
	const auto localCore = outcomeValue ? heroSpellProjection::localReceiptMarginal(coreBoard, kernel) : 0.0f;
	if(inspirationValue) *inspirationValue = core;
	long double weightedDelta = 0.0L;
	for(const auto & outcome : outcomes)
	{
		const auto after = blinkLandingPositionValue(target, outcome.hex, player, battle, battleState, damageCache);
		weightedDelta += static_cast<long double>(after - before) * outcome.weight;
		if(outcomeValue && inspirationValue)
		{
			auto board = std::make_shared<HypotheticBattle>(environment, battleState);
			board->moveUnit(target->unitId(), outcome.hex);
			outcomeValue(board, {}, false, true);
			*inspirationValue += static_cast<double>(outcome.weight) / outcome.totalWeight
				* (heroSpellProjection::localReceiptMarginal(board, kernel) - localCore);
		}
	}

	return static_cast<float>(weightedDelta / outcomes.front().totalWeight);
}
