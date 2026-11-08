/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "BattleEvaluator.h"
#include "MovementQueryCache.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"

std::shared_ptr<HypotheticBattle> BattleEvaluator::createHypotheticBattle(
	const HypotheticBattle::Subject & source) const
{
	auto result = std::make_shared<HypotheticBattle>(env.get(), source);
	result->setMovementQueryCache(movementMemo);
	return result;
}

BattleEvaluator::BattleEvaluator(
	std::shared_ptr<Environment> env,
	std::shared_ptr<CBattleCallback> cb,
	const battle::Unit * activeStack,
	PlayerColor playerID,
	BattleID battleID,
	BattleSide side,
	float strengthRatio,
	int simulationTurnsCount,
	std::shared_ptr<MovementQueryCache> movementCache)
	: movementMemo(movementCache ? std::move(movementCache) : std::make_shared<MovementQueryCache>()),
	scoreEvaluator(cb->getBattle(battleID), env, strengthRatio, simulationTurnsCount),
	cb(cb), env(env), playerID(playerID), battleID(battleID), side(side),
	strengthRatio(strengthRatio), simulationTurnsCount(simulationTurnsCount)
{
	hb = createHypotheticBattle(cb->getBattle(battleID));
	damageCache.buildDamageCache(hb, side);
	targets = std::make_unique<PotentialTargets>(activeStack, damageCache, hb);
}

BattleEvaluator::BattleEvaluator(
	std::shared_ptr<Environment> env,
	std::shared_ptr<CBattleCallback> cb,
	std::shared_ptr<HypotheticBattle> hb,
	DamageCache & damageCache,
	const battle::Unit * activeStack,
	PlayerColor playerID,
	BattleID battleID,
	BattleSide side,
	float strengthRatio,
	int simulationTurnsCount)
	: movementMemo(hb->movementQueryCache() ? hb->movementQueryCache() : std::make_shared<MovementQueryCache>()),
	hb(hb), scoreEvaluator(cb->getBattle(battleID), env, strengthRatio, simulationTurnsCount),
	cb(cb), env(env), playerID(playerID), battleID(battleID), side(side),
	damageCache(damageCache), strengthRatio(strengthRatio), simulationTurnsCount(simulationTurnsCount)
{
	hb->setMovementQueryCache(movementMemo);
	targets = std::make_unique<PotentialTargets>(activeStack, damageCache, hb);
}
