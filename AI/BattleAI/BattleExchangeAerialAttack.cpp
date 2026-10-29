/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "BattleExchangeVariant.h"
#include "AerialRetaliationProjection.h"
std::optional<float> BattleExchangeVariant::trackAerialExchange(
	const std::shared_ptr<StackWithBonuses> & attacker,
	const std::shared_ptr<StackWithBonuses> & defender, bool shooting, bool isOurAttack,
	DamageCache & cache, const std::shared_ptr<HypotheticBattle> & battle,
	bool evaluateOnly, bool allowRetaliation)
{
	if(!shooting || !allowRetaliation || !aerialRetaliationAI::active(*battle, defender.get()))
		return std::nullopt;
	auto branch = evaluateOnly ? std::make_shared<HypotheticBattle>(battle->env, battle) : battle;
	const auto actor = branch->getForUpdate(attacker->unitId());
	const auto target = branch->getForUpdate(defender->unitId());
	const auto preview = AttackPossibility::evaluate(BattleAttackInfo(actor.get(), target.get(), 0, true),
		actor->getPosition(), cache, branch);
	BattleExchangeVariant exchange;
	const auto value = exchange.trackAttack(preview, branch, cache);
	if(!evaluateOnly)
	{
		const auto & score = exchange.getScore();
		dpsScore.ourDamageReduce += isOurAttack ? score.ourDamageReduce : score.enemyDamageReduce;
		dpsScore.enemyDamageReduce += isOurAttack ? score.enemyDamageReduce : score.ourDamageReduce;
	}
	return value;
}
