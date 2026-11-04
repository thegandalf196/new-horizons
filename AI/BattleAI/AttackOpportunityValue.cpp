/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "AttackPossibility.h"
int64_t AttackPossibility::evaluateBlockedShootersDmg(
	const BattleAttackInfo & attackInfo,
	BattleHex hex,
	DamageCache & damageCache,
	std::shared_ptr<CBattleInfoCallback> state)
{
	int64_t res = 0;

	if(attackInfo.shooting)
		return 0;

	std::set<uint32_t> checkedUnits;

	auto attacker = attackInfo.attacker;
	const auto & hexes = attacker->getSurroundingHexes(hex);
	for(const BattleHex & tile : hexes)
	{
		auto st = state->battleGetUnitByPos(tile, true);
		if(!st || !state->battleMatchOwner(st, attacker))
			continue;
		if(vstd::contains(checkedUnits, st->unitId()))
			continue;
		if(!state->battleCanShoot(st))
			continue;

		checkedUnits.insert(st->unitId());

		// FIXME: provide distance info for Jousting bonus
		BattleAttackInfo rangeAttackInfo(st, attacker, 0, true);
		rangeAttackInfo.defenderPos = hex;

		BattleAttackInfo meleeAttackInfo(st, attacker, 0, false);
		meleeAttackInfo.defenderPos = hex;

		auto rangeDmg = state->battleExpectedLuckDamage(rangeAttackInfo);
		auto meleeDmg = state->battleExpectedLuckDamage(meleeAttackInfo);
		auto cachedDmg = damageCache.getOriginalDamage(st, attacker, state);

		int64_t gain = rangeDmg - meleeDmg + 1;
		res += gain * cachedDmg / std::max<int64_t>(1, rangeDmg);
	}

	return res;
}
