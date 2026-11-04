/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "TemporaryGatingProjection.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/DemonicGateArrival.h"
#include "../../lib/battle/ReachabilityInfo.h"
#include "../../lib/json/JsonNode.h"
#include <limits>
namespace temporaryGatingAI
{
namespace
{
float arrivingAttack(const std::shared_ptr<HypotheticBattle> & state, const battle::Unit & actor,
	const battle::Unit * target, DamageCache & cache, Workload & work)
{
	if(!target || !target->alive()) return 0;
	const bool shooting = state->battleCanShoot(&actor, target->getPosition());
	BattleHex from = actor.getPosition();
	if(!shooting)
	{
		++work.arrivingReachability;
		const auto reachability = state->getReachability(&actor);
		uint32_t best = ReachabilityInfo::INFINITE_DIST;
		from = BattleHex::INVALID;
		// Native adjacent footprints, including the alternate front of a wide
		// attacker whose tail touches the target: at most sixteen anchors.
		for(const auto adjacent : target->getSurroundingHexes())
		for(const auto candidate : {adjacent, actor.doubleWide() ? battle::Unit::occupiedHex(
			adjacent, true, CBattleInfoCallback::otherSide(actor.unitSide())) : BattleHex(BattleHex::INVALID)})
		{
			if(!candidate.isAvailable() || !reachability.accessibility.accessible(candidate, &actor)
				|| reachability.distances[candidate.toInt()] > actor.getMovementRange(0)
				|| state->meleeAttackHexes(&actor, target, candidate).empty()) continue;
			const auto distance = reachability.distances[candidate.toInt()];
			if(distance < best || (distance == best && candidate.toInt() < from.toInt()))
			{ best = distance; from = candidate; }
		}
		if(!from.isAvailable()) return 0;
	}
	const BattleAttackInfo attack(&actor, target, 0, shooting);
	++work.arrivingAttacks;
	return AttackPossibility::evaluate(attack, from, cache, state).attackValue();
}
std::optional<float> outcomeValue(const Environment * environment,
	const std::shared_ptr<HypotheticBattle> & parent, const battle::Unit & source, BattleSide side,
	const Candidate & candidate, const DamageCache & damage, float ordinaryValue, int rounds, Workload & work)
{
	auto state = std::make_shared<HypotheticBattle>(environment, parent);
	if(state->usesOwnedFrameQueries()) return std::nullopt;
	DamageCache cache(&damage);
	// Retain the reference-HP penalty if a supplied source differs from this branch.
	const auto * moved = state->battleGetUnitByID(source.unitId());
	const auto lostHealth = source.getAvailableHealth() - (moved ? moved->getAvailableHealth() : 0);
	const float movementCost = lostHealth > 0 ? AttackPossibility::calculateDamageReduce(nullptr,
		&source, lostHealth, cache, state) : 0;
	if(!moved || !moved->alive()) return -std::max(0.0f, ordinaryValue) - movementCost;
	++work.postMovementPlans;
	const auto plan = newHorizonsDemonicGating::plan(*state, candidate.action);
	if(!plan || !plan->temporary) return -std::max(0.0f, ordinaryValue) - movementCost;
	battle::UnitInfo info;
	info.id = state->nextUnitId();
	info.count = plan->count;
	info.type = plan->creature;
	info.side = side;
	info.position = plan->position;
	info.summoned = true;
	JsonNode data;
	info.save(data);
	state->addUnit(info.id, data);
	auto gated = state->getForUpdate(info.id);
	const auto perks = newHorizonsDemonicGating::arrivalPerks(state->battleGetFightingHero(side));
	if(perks.reinforced)
		gated->health.addTemporaryHitPoints(newHorizonsDemonicGating::reinforcedHitPoints(gated->getAvailableHealth()));
	const bool beacon = perks.beacon && newHorizonsDemonicGating::beaconEligible(*state, *gated, side);
	state->addUnitBonus(info.id, newHorizonsDemonicGating::arrivalBonuses(perks, plan->atEndOfRound, beacon));
	float fire = 0;
	if(perks.hellfire)
	{
		const auto enemies = newHorizonsDemonicGating::hellfireTargets(*state, *gated, side, true);
		const auto raw = newHorizonsDemonicGating::hellfireRawPerEnemy(gated->getAvailableHealth(), enemies.size());
		for(const auto * enemy : enemies)
		{
			auto victim = state->getForUpdate(enemy->unitId());
			auto hit = std::min(victim->getAvailableHealth(), newHorizonsDemonicGating::hellfireDamage(*state, *victim, raw, true));
			fire += AttackPossibility::calculateDamageReduce(nullptr, victim.get(), hit, cache, state);
			victim->damage(hit, false, battle::DamageProvenance::SPELL);
		}
	}
	const int horizon = std::max(1, rounds);
	// End-of-round arrivals first act next round as well; only their arrival
	// damage occurs sooner. This is a nominal finite-horizon weight, not a queue.
	const int actionDelay = std::max(1, plan->arrivalRound - parent->getRound());
	const int fireDelay = plan->atEndOfRound ? 0 : actionDelay;
	const float actionWeight = std::max(0, horizon - actionDelay) / static_cast<float>(horizon);
	const float fireWeight = std::max(0, horizon - fireDelay) / static_cast<float>(horizon);
	const auto attack = arrivingAttack(state, *gated, state->battleGetUnitByID(candidate.threatUnitId), cache, work);
	return attack * actionWeight + fire * fireWeight - movementCost
		- (plan->freeActivation ? 0 : std::max(0.0f, ordinaryValue));
}
}
std::optional<BattleAction> projectedChoice(const Environment * environment,
	const std::shared_ptr<HypotheticBattle> & parent, const battle::Unit & source, BattleSide side,
	const DamageCache & damage, float ordinaryActivationValue, int forecastRounds, Workload * output)
{
	Workload work;
	if(!parent || parent->usesOwnedFrameQueries()) { if(output) *output = work; return std::nullopt; }
	const auto proposals = candidates(*parent, source, side, &work);
	bool complete = false;
	const auto selected = choose(proposals, [&](const auto & candidate)
	{ return outcomeValue(environment, parent, source, side, candidate, damage, ordinaryActivationValue, forecastRounds, work); },
	complete, &work);
	if(output) *output = work;
	return complete ? selected : std::nullopt;
}
}
