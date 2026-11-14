/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleInfo.h"
#include "../CRandomGenerator.h"
#include <stdexcept>

int64_t BattleInfo::getActualDamage(const DamageRange & damage, int32_t attackerCount, vstd::RNG & rng) const
{
	if(attackerCount <= 0 || damage.min < 0 || damage.max < damage.min)
		throw std::invalid_argument("Damage sampling requires a positive stack and nonnegative ordered range");
	if(damage.min == damage.max) return damage.min;
	const int32_t samples = std::min<int32_t>(10, attackerCount);
	int64_t quotient = 0, remainder = 0;
	for(int32_t index = 0; index < samples; ++index)
	{
		const int64_t value = rng.nextInt64(damage.min, damage.max);
		quotient += value / samples;
		remainder += value % samples;
	}
	// The quotient sum and final mean cannot exceed the largest sampled value.
	return quotient + remainder / samples;
}
