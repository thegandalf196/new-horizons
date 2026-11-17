/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CHealthValueContext.h"
#include "CHealth.h"

#include <limits>
#include <stdexcept>

namespace battle
{
void CHealthValueContext::validate() const
{
	if(baseAmount < 0 || maximumHealth <= 0)
		throw std::invalid_argument("Invalid detached health owner facts");
}

CHealth CHealth::detached(const CHealth & source, const CHealthValueContext context)
{
	context.validate();
	if(context != source.valueContext())
		throw std::invalid_argument("Detached health owner facts do not match the source");
	CHealth result(source);
	result.owner = nullptr;
	result.detachedContext = context;
	result.validateState();
	return result;
}

bool CHealth::isDetached() const noexcept
{
	return owner == nullptr;
}

CHealthValueContext CHealth::valueContext() const
{
	return {baseAmount(), checkedMaximumHealth()};
}

void CHealth::updateDetachedContext(const CHealthValueContext context)
{
	if(!isDetached())
		throw std::logic_error("Cannot replace a native Unit health context");
	context.validate();
	// Base amount owns casualty provenance and is immutable within a forecast.
	// Reinforcements use appendTemporaryCreatures, never a rewritten original army.
	if(context.baseAmount != detachedContext.baseAmount)
		throw std::invalid_argument("Cannot rewrite a forecast's original creature count");
	detachedContext = context;
}

int32_t CHealth::baseAmount() const
{
	return owner ? owner->unitBaseAmount() : detachedContext.baseAmount;
}

uint32_t CHealth::currentMaximumHealth() const
{
	return owner ? owner->getMaxHealth() : static_cast<uint32_t>(detachedContext.maximumHealth);
}

int32_t CHealth::checkedMaximumHealth() const
{
	const uint32_t maximum = currentMaximumHealth();
	if(maximum == 0 || maximum > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
		throw std::runtime_error("Creature health capacity is outside the supported range");
	return static_cast<int32_t>(maximum);
}

void CHealth::validateState() const
{
	const int64_t originalMaximum = totalHealthOverride > 0
		? totalHealthOverride + shadowGiftMaximumHealthLost
		: static_cast<int64_t>(maximumPerCreature()) * baseAmount();
	if(shadowGiftMaximumHealthLost < 0 || shadowGiftMaximumHealthLost > originalMaximum)
		throw std::runtime_error("Invalid Shadow Gift maximum-health loss");
	if(!capacityHealthTracking && (capacityHealthMax != 0 || !capacityHealthCohorts.empty()))
		throw std::runtime_error("Invalid inactive capacity health ledger");
	if(capacityHealthTracking && capacityHealthMax <= 0)
		throw std::runtime_error("Invalid capacity health maximum");
	if(capacityHealthMaxFixed && !capacityHealthTracking)
		throw std::runtime_error("Invalid fixed battle-form provenance capacity");
	if(totalHealthOverride < 0 || (totalHealthOverride > 0 && capacityHealthMaxFixed))
		throw std::runtime_error("Invalid battle-form health total override");
	int64_t cohortCount = 0;
	int32_t previousHitPoints = 0;
	for(const auto & cohort : capacityHealthCohorts)
	{
		if(cohort.hitPoints <= 0 || cohort.count <= 0 || cohort.hitPoints > capacityHealthMax
			|| cohort.hitPoints <= previousHitPoints)
			throw std::runtime_error("Invalid capacity health cohort");
		previousHitPoints = cohort.hitPoints;
		cohortCount += cohort.count;
		if(cohortCount > std::numeric_limits<int32_t>::max())
			throw std::runtime_error("Capacity health cohort count overflow");
	}
	const int64_t expectedCount = static_cast<int64_t>(fullUnits)
		+ (firstHPleft > 0 ? 1 : 0) + cohortCount;
	if(firstHPleft < 0 || fullUnits < 0 || resurrected < 0 || unusableRemains < 0
		|| unusableRemains > baseAmount()
		|| (capacityHealthTracking && firstHPleft > capacityHealthMax)
		|| (capacityHealthTracking && firstHPleft == 0 && expectedCount > 0)
		|| expectedCount > std::numeric_limits<int32_t>::max())
		throw std::runtime_error("Invalid capacity health count state");
	validateCasualtyProvenanceLedger();
}
}
