/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CreatureAbilityChance.h"
#include "../bonuses/IBonusBearer.h"
#include <algorithm>
namespace creatureAbilityChance
{
Rounding rounding(const IBonusBearer & unit, int percentageChance)
{
	const int chance = unit.favorableCreatureAbilityChanceBasisPoints(percentageChance);
	if(chance == std::clamp(percentageChance, 0, 100) * 100)
		return {percentageChance, 0};
	return {chance / 100, chance % 100};
}
}
