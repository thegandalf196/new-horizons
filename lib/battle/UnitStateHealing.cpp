/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CUnitState.h"
#include "HealingReceipt.h"
#include "../bonuses/BonusList.h"
#include <algorithm>
#include <stdexcept>
namespace battle
{

int64_t Unit::healingCapacity(const int64_t requested, const EHealLevel level, const EHealPower power) const
{
	const auto state = acquireState();
	return state ? state->healingCapacity(requested, level, power) : 0;
}

int64_t CUnitState::rawHealingCapacity(int64_t requested, const EHealLevel level, const EHealPower power) const
{
	if(battleFormOriginalHealth.isBattleFormProvenance())
	{
		CHealth sourceProbe = battleFormOriginalHealth;
		CHealth effectiveProbe = health;
		int64_t effectiveAllowed = requested;
		sourceProbe.heal(requested, level, power, true);
		effectiveProbe.heal(effectiveAllowed, level, power, false);
		return std::min(requested, effectiveAllowed);
	}
	CHealth probe = health;
	probe.heal(requested, level, power);
	return requested;
}

int64_t CUnitState::healingCapacity(const int64_t requested, const EHealLevel level, const EHealPower power) const
{
	if(requested <= 0 || isTimeStopped() || phantomInitialIntegrity > 0 || cloned
		|| (level == EHealLevel::HEAL && power == EHealPower::ONE_BATTLE))
		return 0;
	return rawHealingCapacity(requested, level, power);
}

HealInfo CUnitState::heal(int64_t & amount, EHealLevel level, EHealPower power)
{
	return applyHealing(amount, level, power, level != EHealLevel::OVERHEAL);
}

HealInfo CUnitState::recreateAfterNativeRebirth(int64_t & amount)
{
	return applyHealing(amount, EHealLevel::RESURRECT, EHealPower::PERMANENT, false);
}

HealInfo CUnitState::applyHealing(int64_t & amount, EHealLevel level, EHealPower power, bool receivedBenefit)
{
	if(isTimeStopped())
	{
		amount = 0;
		return {};
	}

	if(phantomInitialIntegrity > 0)
	{
		amount = 0;
		return {};
	}

	if(level == EHealLevel::HEAL && power == EHealPower::ONE_BATTLE)
		logGlobal->error("Heal for one battle does not make sense");
	else if(cloned)
		logGlobal->error("Attempt to heal clone");
	else
	{
		const int32_t reduction = receivedBenefit ? healingReceivedReduction(*this) : 0;
		if(battleFormOriginalHealth.isBattleFormProvenance())
		{
			const int64_t eligible = rawHealingCapacity(amount, level, power);
			const int64_t accepted = reduction > 0 ? receivedHealing(eligible, reduction) : eligible;

			CHealth nextSource = battleFormOriginalHealth;
			CHealth nextEffective = health;
			int64_t sourceAmount = accepted;
			int64_t effectiveAmount = accepted;
			nextSource.heal(sourceAmount, level, power, true);
			const HealInfo result = nextEffective.heal(effectiveAmount, level, power, false);
			if(sourceAmount != accepted || effectiveAmount != accepted)
				throw std::logic_error("Battle-form health ledgers disagree on accepted healing");

			battleFormOriginalHealth = nextSource;
			health = nextEffective;
			amount = accepted;
			normalizeCapacityHealth();
			return result;
		}

		if(reduction > 0)
			amount = receivedHealing(rawHealingCapacity(amount, level, power), reduction);
		auto result = health.heal(amount, level, power);
		normalizeCapacityHealth();
		return result;
	}

	return {};
}
int64_t CUnitState::capacityRegenerationProjectedHeal() const
{
	if(!alive() || capacityHealthReferenceMax <= 0 || !health.isCapacityHealthTracking())
		return 0;
	const int64_t numerator = static_cast<int64_t>(getMaxHealth()) + capacityRegenerationRemainderTenths;
	const auto perCreatureHeal = static_cast<int32_t>(numerator / 10);
	const int64_t eligible = health.capacityRegenerationProjectedHeal(perCreatureHeal);
	return receivedHealing(eligible, healingReceivedReduction(*this));
}

int64_t CUnitState::consumeCapacityRegeneration()
{
	if(!alive() || capacityHealthReferenceMax <= 0 || !health.isCapacityHealthTracking())
		return 0;
	const int64_t numerator = static_cast<int64_t>(getMaxHealth()) + capacityRegenerationRemainderTenths;
	const auto perCreatureHeal = static_cast<int32_t>(numerator / 10);
	const int32_t reduction = healingReceivedReduction(*this);
	const int64_t eligible = reduction > 0 ? health.capacityRegenerationProjectedHeal(perCreatureHeal) : 0;
	const int64_t received = reduction > 0 ? receivedHealing(eligible, reduction) : 0;
	capacityRegenerationRemainderTenths = static_cast<int32_t>(numerator % 10);
	return reduction > 0 ? health.consumeCapacityRegeneration(perCreatureHeal, received)
		: health.consumeCapacityRegeneration(perCreatureHeal);
}

}
