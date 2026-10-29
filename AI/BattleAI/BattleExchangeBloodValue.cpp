/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "BattleExchangeVariant.h"
#include "PetrifyingBloodValue.h"
float BattleExchangeVariant::finishPetrifyingBlood(const std::shared_ptr<HypotheticBattle> & hb,
	const battle::CUnitState & actor, const std::vector<petrifyingBloodAI::Pending> & pending,
	bool isOurAttack, DamageCache & damageCache)
{
	const float value = petrifyingBloodAI::resolve(hb, actor, pending, damageCache);
	(isOurAttack ? dpsScore.enemyDamageReduce : dpsScore.ourDamageReduce) += std::max(0.0f, value);
	(isOurAttack ? dpsScore.ourDamageReduce : dpsScore.enemyDamageReduce) += std::max(0.0f, -value);
	return value;
}
