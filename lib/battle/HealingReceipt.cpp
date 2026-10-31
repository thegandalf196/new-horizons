/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HealingReceipt.h"
#include "NewHorizonsGraveDust.h"
#include <stdexcept>
#include <algorithm>
#include "../bonuses/BonusList.h"
#include "../bonuses/IBonusBearer.h"
namespace battle
{
int32_t healingReceivedReduction(const IBonusBearer & unit)
{
	int32_t reduction = 0;
	bool checkedDustGroup = false;
	const auto bonuses = unit.getUnstackedBonuses(Selector::type()(BonusType::HEALING_RECEIVED_REDUCTION_BASIS_POINTS));
	for(const auto & bonus : *bonuses)
	{
		if(newHorizonsGraveDust::validateRecord(*bonus) && !checkedDustGroup)
		{
			newHorizonsGraveDust::validateBearerGroup(unit);
			checkedDustGroup = true;
		}
		reduction = std::max(reduction, bonus->val);
	}
	return reduction;
}
int64_t receivedHealing(const int64_t eligibleHealth, const int32_t reductionBasisPoints)
{
	if(eligibleHealth < 0 || reductionBasisPoints < 0 || reductionBasisPoints > 10'000)
		throw std::invalid_argument("Invalid incoming healing receipt inputs");
	const int64_t prevented = (eligibleHealth / 10'000) * reductionBasisPoints
		+ ((eligibleHealth % 10'000) * reductionBasisPoints) / 10'000;
	return eligibleHealth - prevented;
}
}
