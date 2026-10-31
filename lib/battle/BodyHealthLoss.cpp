/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BodyHealthLoss.h"
#include <algorithm>
#include <stdexcept>
namespace battle
{
int64_t bodyHealthLoss(int64_t appliedDamage, int64_t temporaryHealthBefore)
{
	if(temporaryHealthBefore < 0)
		throw std::invalid_argument("Negative prehit temporary Health");
	return std::max<int64_t>(0, appliedDamage - std::min(std::max<int64_t>(0, appliedDamage), temporaryHealthBefore));
}
}
