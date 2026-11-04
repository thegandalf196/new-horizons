/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleExchangeVariant.h"
#include "ArcaneInspirationProjection.h"
#include "PhysicalStrikeProjection.h"
std::optional<float> BattleExchangeVariant::trackInspirationProbe(
	const std::shared_ptr<StackWithBonuses> & attacker, const std::shared_ptr<StackWithBonuses> & defender,
	bool shooting, bool isOurAttack, DamageCache & damageCache,
	const std::shared_ptr<HypotheticBattle> & battle, bool evaluateOnly, bool allowRetaliation)
{
	if(!evaluateOnly || !arcaneInspirationAI::needsReprisalProjection(attacker.get(), defender.get()))
		return std::nullopt;
	// Only an actual reflection status needs the detached injury/death transaction.
	// Charge-only and ordinary rank ownership retain the existing cheap probe.
	auto child = std::make_shared<HypotheticBattle>(battle->env, battle);
	DamageCache childCache(&damageCache);
	BattleExchangeVariant projected;
	return projected.trackAttack(child->getForUpdate(attacker->unitId()), child->getForUpdate(defender->unitId()),
		shooting, isOurAttack, childCache, child, false, allowRetaliation);
}
std::pair<float, float> BattleExchangeVariant::accountReprisalValue(const PhysicalStrikeProjection & projection,
	DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & battle,
	PlayerColor reference, bool isOurAttack)
{
	const auto value = projection.reprisalValue(damageCache, battle, reference);
	if(isOurAttack)
	{
		dpsScore.ourDamageReduce += value.first;
		dpsScore.enemyDamageReduce += value.second;
	}
	else
	{
		dpsScore.enemyDamageReduce += value.first;
		dpsScore.ourDamageReduce += value.second;
	}
	return value;
}
