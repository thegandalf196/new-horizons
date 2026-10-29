/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "BattleExchangeVariant.h"
#include "DreadCavalryProjection.h"
#include "VisibleCandidateWorld.h"
#include "SpellTargetsEvaluator.h"
#include "../../lib/battle/NewHorizonsDreadCavalry.h"
std::optional<float> BattleExchangeVariant::trackDreadAction(
	const std::shared_ptr<StackWithBonuses> & attacker,
	const std::shared_ptr<StackWithBonuses> & defender, bool shooting, bool isOurAttack,
	DamageCache & cache, const std::shared_ptr<HypotheticBattle> & battle,
	bool evaluateOnly, bool allowRetaliation)
{
	const bool pending = newHorizonsDreadCavalry::hasRawPending(attacker.get());
	if((shooting && !pending) || !allowRetaliation || (!pending
		&& !newHorizonsDreadCavalry::entitlement(*battle, attacker.get())))
		return std::nullopt;
	auto branch = evaluateOnly ? std::make_shared<HypotheticBattle>(battle->env, battle) : battle;
	const auto actor = branch->getForUpdate(attacker->unitId());
	const auto target = branch->getForUpdate(defender->unitId());
	const auto reach = branch->getReachability(actor.get());
	const auto owner = std::make_shared<const VisibleCandidateWorld>(branch);
	std::optional<AttackPossibility> best;
	if(branch->battleCanAttackUnitAction(actor.get(), target.get()))
	{
		for(const auto from : branch->battleGetAvailableHexes(reach, actor.get(), false))
		{
			if(branch->isMeleeAttackPossibleWithLongReach(actor.get(), target.get(), from))
			{
				const int distance = SpellTargetEvaluator::physicalTravelDistance(reach, from);
				if(distance < 0)
					continue;
				auto preview = VisibleCandidateWorld::evaluate(owner, actor.get(), target.get(), distance, false, from, cache);
				if(!best || preview.attackValue() > best->attackValue())
					best = std::move(preview);
			}
		}
	}
	if(!best)
	{
		if(!evaluateOnly) actor->dreadCavalryState.retirePending();
		return 0;
	}
	BattleExchangeVariant exchange;
	const auto value = exchange.trackAttack(*best, branch, cache);
	if(!evaluateOnly)
	{
		const auto & score = exchange.getScore();
		dpsScore.ourDamageReduce += isOurAttack ? score.ourDamageReduce : score.enemyDamageReduce;
		dpsScore.enemyDamageReduce += isOurAttack ? score.enemyDamageReduce : score.ourDamageReduce;
	}
	return value;
}
