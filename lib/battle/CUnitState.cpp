/*
 * CUnitState.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "CUnitState.h"

#include <vcmi/spells/Spell.h>

#include "../CStack.h"
#include "../CCreatureHandler.h"
#include "../mapObjects/army/CArmedInstance.h"
#include "../spells/CSpell.h"

#include "../bonuses/BonusParameters.h"
#include "../spells/NewHorizonsMagic.h"
#include "../spells/NewHorizonsSorcery.h"
#include "../serializer/JsonDeserializer.h"
#include "../serializer/JsonSerializer.h"

namespace battle
{
namespace
{
void addCapacityCount(int32_t & current, const int32_t amount)
{
	if(amount < 0 || static_cast<int64_t>(current) + amount > std::numeric_limits<int32_t>::max())
		throw std::runtime_error("Capacity health count overflow");
	current += amount;
}

int32_t checkedHealthCapacity(const battle::Unit * unit)
{
	const uint32_t maximum = unit->getMaxHealth();
	if(maximum == 0 || maximum > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
		throw std::runtime_error("Creature health capacity is outside the supported range");
	return static_cast<int32_t>(maximum);
}
}

///CAmmo
CAmmo::CAmmo(const battle::Unit * Owner, CSelector totalSelector):
	used(0),
	owner(Owner),
	totalProxy(Owner, totalSelector)
{
	reset();
}

CAmmo & CAmmo::operator= (const CAmmo & other)
{
	used = other.used;
	return *this;
}

int32_t CAmmo::available() const
{
	return total() - used;
}

bool CAmmo::canUse(int32_t amount) const
{
	return (available() - amount >= 0) || !isLimited();
}

bool CAmmo::isLimited() const
{
	return true;
}

void CAmmo::reset()
{
	used = 0;
}

int32_t CAmmo::total() const
{
	return totalProxy.getValue();
}

void CAmmo::use(int32_t amount)
{
	if(!isLimited())
		return;

	if(available() - amount < 0)
	{
		logGlobal->error("Stack ammo overuse. total: %d, used: %d, requested: %d", total(), used, amount);
		used += available();
	}
	else
		used += amount;
}

void CAmmo::serializeJson(JsonSerializeFormat & handler)
{
	handler.serializeInt("used", used, 0);
}

///CShots
CShots::CShots(const battle::Unit * Owner)
	: CAmmo(Owner, Selector::type()(BonusType::SHOTS)),
	shooter(Owner, Selector::type()(BonusType::SHOOTER))
{
}

CShots & CShots::operator=(const CShots & other)
{
	// A state copy transfers usage, not its source unit's cache target or environment.
	CAmmo::operator=(other);
	return *this;
}

bool CShots::isLimited() const
{
	return !shooter.hasBonus() || !env->unitHasAmmoCart(owner);
}

void CShots::setEnv(const IUnitEnvironment * env_)
{
	env = env_;
}

int32_t CShots::total() const
{
	if(shooter.hasBonus())
		return CAmmo::total();
	else
		return 0;
}

///CCasts
CCasts::CCasts(const battle::Unit * Owner):
	CAmmo(Owner, Selector::type()(BonusType::CASTS))
{
}

///CRetaliations
CRetaliations::CRetaliations(const battle::Unit * Owner)
	: CAmmo(Owner, Selector::type()(BonusType::ADDITIONAL_RETALIATION)),
	totalCache(0),
	noRetaliation(Owner, Selector::type()(BonusType::SIEGE_WEAPON).Or(Selector::type()(BonusType::NO_RETALIATION))),
	unlimited(Owner, Selector::type()(BonusType::UNLIMITED_RETALIATIONS))
{
}

bool CRetaliations::isLimited() const
{
	return !unlimited.hasBonus() || noRetaliation.hasBonus();
}

int32_t CRetaliations::total() const
{
	if(noRetaliation.hasBonus())
		return 0;

	//after dispel bonus should remain during current round
	int32_t val = 1 + totalProxy.getValue();
	vstd::amax(totalCache, val);
	return totalCache;
}

CRetaliations & CRetaliations::operator=(const CRetaliations & other)
{
	CAmmo::operator=(other);
	totalCache = other.totalCache;
	// Keep the destination's no-retaliation/unlimited bonus cache bindings.
	return *this;
}

void CRetaliations::reset()
{
	CAmmo::reset();
	totalCache = 0;
}

void CRetaliations::serializeJson(JsonSerializeFormat & handler)
{
	CAmmo::serializeJson(handler);
	//we may be serialized in the middle of turn
	handler.serializeInt("totalCache", totalCache, 0);
}

///CHealth
CHealth::CHealth(const battle::Unit * Owner):
	owner(Owner)
{
	reset();
}

CHealth & CHealth::operator=(const CHealth & other)
{
	//do not change owner
	firstHPleft = other.firstHPleft;
	fullUnits = other.fullUnits;
	resurrected = other.resurrected;
	unusableRemains = other.unusableRemains;
	temporaryHitPoints = other.temporaryHitPoints;
	shadowGiftMaximumHealthLost = other.shadowGiftMaximumHealthLost;
	capacityHealthTracking = other.capacityHealthTracking;
	capacityHealthMax = other.capacityHealthMax;
	capacityHealthMaxFixed = other.capacityHealthMaxFixed;
	totalHealthOverride = other.totalHealthOverride;
	capacityHealthCohorts = other.capacityHealthCohorts;
	return *this;
}

void CHealth::init()
{
	reset();
	fullUnits = owner->unitBaseAmount() > 1 ? owner->unitBaseAmount() - 1 : 0;
	firstHPleft = owner->unitBaseAmount() > 0 ? owner->getMaxHealth() : 0;
}

void CHealth::addResurrected(int32_t amount)
{
	resurrected += amount;
	vstd::amax(resurrected, 0);
}

void CHealth::addUnusableRemains(int32_t amount)
{
	if(amount <= 0)
		return;

	unusableRemains += amount;
	vstd::abetween(unusableRemains, 0, owner->unitBaseAmount());
}

int64_t CHealth::available() const
{
	return creatureHealthAvailable() + temporaryHitPoints;
}

int64_t CHealth::creatureHealthAvailable() const
{
	const auto unitHealth = capacityHealthMaxFixed ? capacityHealthMax : owner->getMaxHealth();
	int64_t result = static_cast<int64_t>(firstHPleft)
		+ static_cast<int64_t>(unitHealth) * fullUnits;
	for(const auto & cohort : capacityHealthCohorts)
		result += static_cast<int64_t>(cohort.hitPoints) * cohort.count;
	return result;
}

int64_t CHealth::total() const
{
	if(totalHealthOverride > 0)
		return std::max<int64_t>(0, totalHealthOverride - shadowGiftMaximumHealthLost);
	const int64_t originalMaximum = static_cast<int64_t>(maximumPerCreature()) * owner->unitBaseAmount();
	return std::max<int64_t>(0, originalMaximum - shadowGiftMaximumHealthLost);
}

int32_t CHealth::maximumPerCreature() const
{
	if(capacityHealthMaxFixed)
		return capacityHealthMax;
	return checkedHealthCapacity(owner);
}

void CHealth::damage(int64_t & amount)
{
	damage(amount, false, false);
}

void CHealth::damage(int64_t & amount, const bool destroyRemains)
{
	damage(amount, destroyRemains, false);
}

void CHealth::damage(int64_t & amount, const bool destroyRemains, const bool bypassTemporaryHitPoints)
{
	const int32_t oldCount = getCount();
	const int64_t eligibleHealth = bypassTemporaryHitPoints ? creatureHealthAvailable() : available();
	amount = std::clamp<int64_t>(amount, 0, eligibleHealth);
	const int64_t absorbed = bypassTemporaryHitPoints ? 0 : std::min(amount, temporaryHitPoints);
	temporaryHitPoints -= absorbed;
	int64_t creatureDamage = amount - absorbed;

	if(capacityHealthTracking && creatureDamage > 0)
	{
		damageCapacityHealth(creatureDamage);
	}
	else if(creatureDamage >= firstHPleft && creatureDamage > 0)
	{
		int64_t totalHealth = creatureHealthAvailable() - creatureDamage;
		if(totalHealth <= 0)
		{
			fullUnits = 0;
			firstHPleft = 0;
		}
		else
		{
			setFromTotal(totalHealth);
		}
	}
	else if(creatureDamage > 0)
	{
		firstHPleft -= static_cast<int32_t>(creatureDamage);
	}

	addResurrected(getCount() - oldCount);

	if(destroyRemains)
		addUnusableRemains(oldCount - getCount());
}

HealInfo CHealth::heal(int64_t & amount, EHealLevel level, EHealPower power)
{
	const int32_t unitHealth = maximumPerCreature();
	const int32_t oldCount = getCount();

	int64_t maxHeal = std::numeric_limits<int64_t>::max();

	switch(level)
	{
	case EHealLevel::HEAL:
		maxHeal = capacityHealthTracking
			? static_cast<int64_t>(getCount()) * unitHealth - creatureHealthAvailable()
			: (getCount() > 0 ? std::max(0, unitHealth - firstHPleft) : 0);
		if(shadowGiftMaximumHealthLost > 0)
			maxHeal = std::min(maxHeal, total() - creatureHealthAvailable());
		break;
	case EHealLevel::RESURRECT:
		maxHeal = total() - creatureHealthAvailable();
		maxHeal -= static_cast<int64_t>(unusableRemains) * unitHealth;
		break;
	default:
		assert(level == EHealLevel::OVERHEAL);
		// Preserve legacy Overheal behaviour for ordinary units. Once Shadow Gift
		// has reduced this stack's battle cap, healing cannot grow it past that cap.
		if(shadowGiftMaximumHealthLost > 0)
			maxHeal = total() - creatureHealthAvailable();
		break;
	}

	vstd::amax(maxHeal, 0);
	vstd::abetween(amount, int64_t(0), maxHeal);

	if(amount == 0)
		return {};
	if(capacityHealthTracking)
	{
		healCapacityHealth(amount, level);
		if(power == EHealPower::ONE_BATTLE)
			addResurrected(getCount() - oldCount);
		else
			assert(power == EHealPower::PERMANENT);
		return HealInfo(amount, getCount() - oldCount);
	}

	int64_t availableHealth = creatureHealthAvailable();

	availableHealth	+= amount;
	setFromTotal(availableHealth);

	if(power == EHealPower::ONE_BATTLE)
		addResurrected(getCount() - oldCount);
	else
		assert(power == EHealPower::PERMANENT);

	return HealInfo(amount, getCount() - oldCount);
}

void CHealth::setFromTotal(const int64_t totalHealth)
{
	const int32_t unitHealth = owner->getMaxHealth();
	firstHPleft = totalHealth % unitHealth;
	fullUnits = static_cast<int32_t>(totalHealth / unitHealth);

	if(firstHPleft == 0 && fullUnits >= 1)
	{
		firstHPleft = unitHealth;
		fullUnits -= 1;
	}
}

void CHealth::reset(bool clearUnusableRemains)
{
	fullUnits = 0;
	firstHPleft = 0;
	resurrected = 0;
	temporaryHitPoints = 0;
	shadowGiftMaximumHealthLost = 0;
	capacityHealthTracking = false;
	capacityHealthMax = 0;
	capacityHealthMaxFixed = false;
	totalHealthOverride = 0;
	capacityHealthCohorts.clear();
	if(clearUnusableRemains)
		unusableRemains = 0;
}

int32_t CHealth::getCount() const
{
	int64_t result = static_cast<int64_t>(fullUnits) + (firstHPleft > 0 ? 1 : 0);
	for(const auto & cohort : capacityHealthCohorts)
		result += cohort.count;
	if(result < 0 || result > std::numeric_limits<int32_t>::max())
		throw std::runtime_error("Capacity health count overflow");
	return static_cast<int32_t>(result);
}

int32_t CHealth::getFirstHPleft() const
{
	return firstHPleft;
}

int32_t CHealth::getResurrected() const
{
	return resurrected;
}

int32_t CHealth::getUnusableRemains() const
{
	return unusableRemains;
}

int64_t CHealth::getTemporaryHitPoints() const
{
	return temporaryHitPoints;
}

void CHealth::addTemporaryHitPoints(int64_t amount)
{
	if(amount > 0)
		temporaryHitPoints += amount;
}

int64_t CHealth::getCreatureHealthAvailable() const
{
	return creatureHealthAvailable();
}

int64_t CHealth::getTotalHealthOverride() const
{
	return totalHealthOverride;
}

int64_t CHealth::getShadowGiftMaximumHealthLost() const
{
	return shadowGiftMaximumHealthLost;
}

void CHealth::addShadowGiftMaximumHealthLoss(const int64_t amount)
{
	if(amount < 0)
		throw std::invalid_argument("Negative Shadow Gift maximum-health loss");
	const int64_t originalMaximum = totalHealthOverride > 0
		? totalHealthOverride + shadowGiftMaximumHealthLost
		: static_cast<int64_t>(maximumPerCreature()) * owner->unitBaseAmount();
	shadowGiftMaximumHealthLost += std::min(amount, std::max<int64_t>(0, originalMaximum - shadowGiftMaximumHealthLost));
}

void CHealth::repartitionForBattleForm(const int32_t newMaximum, const int64_t originalTotalHealth,
	const int64_t remainingHealth)
{
	if(newMaximum <= 0 || originalTotalHealth < 0 || remainingHealth < 0)
		throw std::invalid_argument("Invalid battle-form health capacity");
	capacityHealthTracking = false;
	capacityHealthMax = 0;
	capacityHealthMaxFixed = false;
	capacityHealthCohorts.clear();
	shadowGiftMaximumHealthLost = 0;
	unusableRemains = 0;
	resurrected = 0;
	totalHealthOverride = originalTotalHealth;
	firstHPleft = static_cast<int32_t>(remainingHealth % newMaximum);
	fullUnits = static_cast<int32_t>(remainingHealth / newMaximum);
	if(firstHPleft == 0 && fullUnits > 0)
	{
		firstHPleft = newMaximum;
		--fullUnits;
	}
}

void CHealth::preserveBattleFormProvenance(const int32_t sourceMaximum)
{
	if(sourceMaximum <= 0)
		throw std::invalid_argument("Invalid battle-form provenance capacity");
	if(fullUnits > 0)
		addCapacityHealth(sourceMaximum, fullUnits);
	fullUnits = 0;
	capacityHealthTracking = true;
	capacityHealthMax = sourceMaximum;
	capacityHealthMaxFixed = true;
	normalizeCapacityHealthCohorts();
	for(auto it = capacityHealthCohorts.begin(); it != capacityHealthCohorts.end();)
	{
		if(it->hitPoints == sourceMaximum)
		{
			addCapacityCount(fullUnits, it->count);
			it = capacityHealthCohorts.erase(it);
		}
		else
			++it;
	}
	promoteCapacityHealthFront();
}

void CHealth::releaseBattleFormProvenance(const bool preserveCapacityTracking)
{
	if(!capacityHealthMaxFixed)
		return;
	capacityHealthMaxFixed = false;
	normalizeCapacityHealth(preserveCapacityTracking);
}

void CHealth::setTemporaryHitPoints(const int64_t amount)
{
	if(amount < 0)
		throw std::invalid_argument("Negative temporary hit points");
	temporaryHitPoints = amount;
}

bool CHealth::isBattleFormProvenance() const
{
	return capacityHealthMaxFixed;
}

void CHealth::takeResurrected()
{
	if(resurrected != 0)
	{
		if(capacityHealthTracking)
		{
			int32_t toRemove = std::min(resurrected, getCount());
			int64_t restoredHealth = 0;
			if(toRemove > 0 && firstHPleft > 0)
			{
				restoredHealth += firstHPleft;
				--toRemove;
			}
			if(toRemove > 0)
			{
				for(const auto & cohort : capacityHealthCohorts)
				{
					const int32_t selected = std::min(toRemove, cohort.count);
					restoredHealth += static_cast<int64_t>(selected) * cohort.hitPoints;
					toRemove -= selected;
					if(toRemove == 0)
						break;
				}
			}
			if(toRemove > 0)
				restoredHealth += static_cast<int64_t>(toRemove) * capacityHealthMax;
			damageCapacityHealth(restoredHealth);
		}
		else
		{
			int64_t totalHealth = creatureHealthAvailable();
			totalHealth -= static_cast<int64_t>(resurrected) * owner->getMaxHealth();
			vstd::amax(totalHealth, 0);
			setFromTotal(totalHealth);
		}
		resurrected = 0;
	}
}

void CHealth::CapacityHealthCohort::serializeJson(JsonSerializeFormat & handler)
{
	handler.serializeInt("hitPoints", hitPoints, 0);
	handler.serializeInt("count", count, 0);
}

void CHealth::addCapacityHealth(const int32_t hitPoints, const int32_t count)
{
	if(hitPoints <= 0 || count <= 0)
		return;
	capacityHealthCohorts.push_back({hitPoints, count});
}

void CHealth::normalizeCapacityHealthCohorts()
{
	std::sort(capacityHealthCohorts.begin(), capacityHealthCohorts.end(), [](const auto & left, const auto & right)
	{
		return left.hitPoints < right.hitPoints;
	});
	std::vector<CapacityHealthCohort> merged;
	merged.reserve(capacityHealthCohorts.size());
	for(const auto & cohort : capacityHealthCohorts)
	{
		if(cohort.hitPoints <= 0 || cohort.count <= 0)
			continue;
		if(!merged.empty() && merged.back().hitPoints == cohort.hitPoints)
			addCapacityCount(merged.back().count, cohort.count);
		else
			merged.push_back(cohort);
	}
	capacityHealthCohorts = std::move(merged);
}

void CHealth::promoteCapacityHealthFront()
{
	if(firstHPleft > 0)
		return;
	if(fullUnits > 0)
	{
		addCapacityHealth(capacityHealthMaxFixed ? capacityHealthMax : owner->getMaxHealth(), fullUnits);
		fullUnits = 0;
	}
	normalizeCapacityHealthCohorts();
	if(capacityHealthCohorts.empty())
		return;
	auto & front = capacityHealthCohorts.front();
	firstHPleft = front.hitPoints;
	if(--front.count == 0)
		capacityHealthCohorts.erase(capacityHealthCohorts.begin());
}

void CHealth::damageCapacityHealth(int64_t amount)
{
	if(amount <= 0)
		return;
	if(firstHPleft > 0)
	{
		if(amount < firstHPleft)
		{
			firstHPleft -= static_cast<int32_t>(amount);
			return;
		}
		amount -= firstHPleft;
		firstHPleft = 0;
	}
	if(fullUnits > 0)
	{
		addCapacityHealth(capacityHealthMax, fullUnits);
		fullUnits = 0;
	}
	normalizeCapacityHealthCohorts();

	std::vector<CapacityHealthCohort> survivors;
	survivors.reserve(capacityHealthCohorts.size() + 1);
	for(const auto & cohort : capacityHealthCohorts)
	{
		if(amount <= 0)
		{
			survivors.push_back(cohort);
			continue;
		}
		const int64_t killed = std::min<int64_t>(cohort.count, amount / cohort.hitPoints);
		amount -= killed * cohort.hitPoints;
		const int32_t remainingCount = cohort.count - static_cast<int32_t>(killed);
		if(remainingCount > 0 && amount > 0)
		{
			const int32_t woundedHealth = cohort.hitPoints - static_cast<int32_t>(amount);
			survivors.push_back({woundedHealth, 1});
			if(remainingCount > 1)
				survivors.push_back({cohort.hitPoints, remainingCount - 1});
			amount = 0;
		}
		else if(remainingCount > 0)
		{
			survivors.push_back({cohort.hitPoints, remainingCount});
		}
	}
	capacityHealthCohorts = std::move(survivors);
	normalizeCapacityHealthCohorts();
	promoteCapacityHealthFront();
}

void CHealth::preserveCapacityHealth()
{
	if(!capacityHealthTracking)
	{
		capacityHealthMax = checkedHealthCapacity(owner);
		capacityHealthTracking = true;
	}
	if(capacityHealthMax <= 0)
		throw std::runtime_error("Cannot preserve health with non-positive creature capacity");
	if(fullUnits > 0)
		addCapacityHealth(capacityHealthMax, fullUnits);
	fullUnits = 0;
	normalizeCapacityHealthCohorts();
}

void CHealth::normalizeCapacityHealth(const bool preserveCapacityTracking)
{
	if(!capacityHealthTracking || capacityHealthMaxFixed)
		return;
	const int32_t newMaximum = checkedHealthCapacity(owner);
	if(newMaximum <= 0 || capacityHealthMax <= 0)
		throw std::runtime_error("Invalid tracked creature health capacity");
	if(capacityHealthMax != newMaximum)
	{
		// Full units are full at the previous capacity. Convert them before reading
		// the new maximum, otherwise a capacity decrease would rewrite their HP.
		if(fullUnits > 0)
			addCapacityHealth(capacityHealthMax, fullUnits);
		fullUnits = 0;
		firstHPleft = std::min(firstHPleft, newMaximum);
		for(auto & cohort : capacityHealthCohorts)
			cohort.hitPoints = std::min(cohort.hitPoints, newMaximum);
		capacityHealthMax = newMaximum;
	}
	normalizeCapacityHealthCohorts();
	for(auto it = capacityHealthCohorts.begin(); it != capacityHealthCohorts.end();)
	{
		if(it->hitPoints == newMaximum)
		{
			addCapacityCount(fullUnits, it->count);
			it = capacityHealthCohorts.erase(it);
		}
		else
			++it;
	}
	if(!preserveCapacityTracking
		&& std::ranges::all_of(capacityHealthCohorts, [newMaximum](const auto & cohort)
		{
			return cohort.hitPoints == newMaximum;
		}))
	{
		capacityHealthCohorts.clear();
		capacityHealthTracking = false;
		capacityHealthMax = 0;
	}
}

int64_t CHealth::capacityRegenerationProjectedHeal(const int32_t perCreatureHeal) const
{
	if(!capacityHealthTracking || capacityHealthMaxFixed || perCreatureHeal <= 0)
		return 0;
	const int32_t maximum = checkedHealthCapacity(owner);
	int64_t result = 0;
	if(firstHPleft > 0)
		result += std::min(perCreatureHeal, maximum - firstHPleft);
	for(const auto & cohort : capacityHealthCohorts)
		result += static_cast<int64_t>(std::min(perCreatureHeal, maximum - cohort.hitPoints)) * cohort.count;
	return result;
}

int64_t CHealth::consumeCapacityRegeneration(const int32_t perCreatureHeal)
{
	const int64_t actualHeal = capacityRegenerationProjectedHeal(perCreatureHeal);
	addCapacityHealthHealing(perCreatureHeal);
	return actualHeal;
}

bool CHealth::isCapacityHealthTracking() const
{
	return capacityHealthTracking;
}

void CHealth::addCapacityHealthHealing(const int32_t perCreatureHeal)
{
	if(perCreatureHeal <= 0)
		return;
	const int32_t maximum = maximumPerCreature();
	if(firstHPleft > 0)
		firstHPleft = static_cast<int32_t>(std::min<int64_t>(maximum,
			static_cast<int64_t>(firstHPleft) + perCreatureHeal));
	std::vector<CapacityHealthCohort> updated;
	updated.reserve(capacityHealthCohorts.size());
	for(const auto & cohort : capacityHealthCohorts)
		updated.push_back({static_cast<int32_t>(std::min<int64_t>(maximum,
			static_cast<int64_t>(cohort.hitPoints) + perCreatureHeal)), cohort.count});
	capacityHealthCohorts = std::move(updated);
	for(auto it = capacityHealthCohorts.begin(); it != capacityHealthCohorts.end();)
	{
		if(it->hitPoints == maximum)
		{
			addCapacityCount(fullUnits, it->count);
			it = capacityHealthCohorts.erase(it);
		}
		else
			++it;
	}
	normalizeCapacityHealthCohorts();
}

void CHealth::healCapacityHealth(int64_t & amount, const EHealLevel level)
{
	const int32_t maximum = maximumPerCreature();
	int64_t remaining = amount;
	if(firstHPleft > 0 && remaining > 0)
	{
		const int32_t healed = static_cast<int32_t>(std::min<int64_t>(remaining, maximum - firstHPleft));
		firstHPleft += healed;
		remaining -= healed;
	}
	if(remaining > 0)
	{
		std::vector<CapacityHealthCohort> updated;
		updated.reserve(capacityHealthCohorts.size() + 1);
		for(const auto & cohort : capacityHealthCohorts)
		{
			const int32_t missing = maximum - cohort.hitPoints;
			if(remaining <= 0 || missing <= 0)
			{
				updated.push_back(cohort);
				continue;
			}
			const int64_t fullyHealed = std::min<int64_t>(cohort.count, remaining / missing);
			if(fullyHealed > 0)
			{
				if(fullyHealed > std::numeric_limits<int32_t>::max())
					throw std::runtime_error("Capacity health count overflow");
				addCapacityCount(fullUnits, static_cast<int32_t>(fullyHealed));
				remaining -= fullyHealed * missing;
			}
			int32_t notFullyHealed = cohort.count - static_cast<int32_t>(fullyHealed);
			if(notFullyHealed > 0 && remaining > 0)
			{
				const int32_t partial = static_cast<int32_t>(std::min<int64_t>(remaining, missing));
				updated.push_back({cohort.hitPoints + partial, 1});
				remaining -= partial;
				--notFullyHealed;
			}
			if(notFullyHealed > 0)
				updated.push_back({cohort.hitPoints, notFullyHealed});
		}
		capacityHealthCohorts = std::move(updated);
	}
	if(level != EHealLevel::HEAL && remaining > 0)
	{
		const int64_t restoredFullUnits = remaining / maximum;
		if(restoredFullUnits > std::numeric_limits<int32_t>::max())
			throw std::runtime_error("Capacity health count overflow");
		addCapacityCount(fullUnits, static_cast<int32_t>(restoredFullUnits));
		const int32_t partial = static_cast<int32_t>(remaining % maximum);
		if(partial > 0)
			addCapacityHealth(partial, 1);
		remaining = 0;
	}
	amount -= remaining;
	normalizeCapacityHealthCohorts();
	promoteCapacityHealthFront();
}

void CHealth::serializeJson(JsonSerializeFormat & handler)
{
	handler.serializeInt("firstHPleft", firstHPleft, 0);
	handler.serializeInt("fullUnits", fullUnits, 0);
	handler.serializeInt("resurrected", resurrected, 0);
	handler.serializeInt("unusableRemains", unusableRemains, 0);
	handler.serializeInt("temporaryHitPoints", temporaryHitPoints, 0);
	handler.serializeInt("shadowGiftMaximumHealthLost", shadowGiftMaximumHealthLost, 0);
	handler.serializeBool("capacityHealthTracking", capacityHealthTracking, false);
	handler.serializeInt("capacityHealthMax", capacityHealthMax, 0);
	handler.serializeBool("capacityHealthMaxFixed", capacityHealthMaxFixed, false);
	handler.serializeInt("totalHealthOverride", totalHealthOverride, 0);
	handler.enterArray("capacityHealthCohorts").serializeStruct(capacityHealthCohorts);
	const int64_t originalMaximum = totalHealthOverride > 0
		? totalHealthOverride + shadowGiftMaximumHealthLost
		: static_cast<int64_t>(maximumPerCreature()) * owner->unitBaseAmount();
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
		|| unusableRemains > owner->unitBaseAmount()
		|| (capacityHealthTracking && firstHPleft > capacityHealthMax)
		|| (capacityHealthTracking && firstHPleft == 0 && expectedCount > 0)
		|| expectedCount > std::numeric_limits<int32_t>::max())
		throw std::runtime_error("Invalid capacity health count state");
}

///CUnitState
CUnitState::CUnitState():
	env(nullptr),
	battleFormOriginalHealth(this),
	cloned(false),
	defending(false),
	drainedMana(false),
	fear(false),
	hadMorale(false),
	castSpellThisTurn(false),
	ghost(false),
	ghostPending(false),
	movedThisRound(false),
	pursuitMovementRemaining(0),
	cleaveUsedThisActivation(false),
	noQuarterMoraleActivationsRemaining(0),
	timeStopTurnConsumedFlag(false),
	regenerationRateMillionths(0),
	regenerationPendingMicroHealth(0),
	summoned(false),
	natureSummoned(false),
	waiting(false),
	waitedThisTurn(false),
	battlecraftWaitBonusUsed(false),
	defensiveStanceMeleeBonus(0),
	defensiveStanceRangedBonus(0),
	bulwarkPreemptiveUsed(false),
	bulwarkMireGripApplied(false),
	bulwarkDefendPhysicalDamage(0),
	bulwarkImmovableRound(-1),
	bulwarkToxicSpinesRound(-1),
	physicalPoisonBaseDamage(0),
	physicalPoisonActivationsRemaining(0),
	physicalPoisonSourceStackId(-1),
	casts(this),
	counterAttacks(this),
	health(this),
	shots(this),
	initiativeBasePerTurn(this, Selector::type()(BonusType::STACKS_INITIATIVE_BASE), BonusCacheMode::VALUE),
	initiativeBasePresencePerTurn(this, Selector::type()(BonusType::STACKS_INITIATIVE_BASE), BonusCacheMode::PRESENCE),
	initiativePercentPerTurn(this, Selector::type()(BonusType::STACKS_INITIATIVE), BonusCacheMode::VALUE),
	initiativeFlatPerTurn(this, Selector::type()(BonusType::STACKS_INITIATIVE_FLAT), BonusCacheMode::VALUE),
	stackSpeedPerTurn(this, Selector::type()(BonusType::STACKS_SPEED), BonusCacheMode::VALUE),
	movementRangePerTurn(this, Selector::type()(BonusType::STACKS_MOVEMENT_RANGE), BonusCacheMode::VALUE),
	immobilizedPerTurn(this, Selector::type()(BonusType::SIEGE_WEAPON).Or(Selector::type()(BonusType::BIND_EFFECT)), BonusCacheMode::PRESENCE),
	bonusCache(this),
	cloneID(-1)
{

}

CUnitState & CUnitState::operator=(const CUnitState & other)
{
	//do not change unit and bonus info
	const CreatureID previousEffectiveCreature = battleFormCreature();
	const CreatureID previousOriginalCreature = battleFormOriginalCreature();

	cloned = other.cloned;
	activationMovementBonus = other.activationMovementBonus;
	defending = other.defending;
	drainedMana = other.drainedMana;
	fear = other.fear;
	hadMorale = other.hadMorale;
	castSpellThisTurn = other.castSpellThisTurn;
	ghost = other.ghost;
	ghostPending = other.ghostPending;
	movedThisRound = other.movedThisRound;
	pursuitMovementRemaining = other.pursuitMovementRemaining;
	cleaveUsedThisActivation = other.cleaveUsedThisActivation;
	rangedFollowUpDamagePercent = other.rangedFollowUpDamagePercent;
	archeryCounterfireRound = other.archeryCounterfireRound;
	archeryDeadeyeRound = other.archeryDeadeyeRound;
	archerySuppressionActivationSerial = other.archerySuppressionActivationSerial;
	archeryRainOfArrowsActivationSerial = other.archeryRainOfArrowsActivationSerial;
	archeryCrossfireRound = other.archeryCrossfireRound;
	archeryCrossfireAttackers = other.archeryCrossfireAttackers;
	archeryCrossfireDefenders = other.archeryCrossfireDefenders;
	noQuarterMoraleActivationsRemaining = other.noQuarterMoraleActivationsRemaining;
	capacityRegenerationRemainderTenths = other.capacityRegenerationRemainderTenths;
	timeStopTurnConsumedFlag = other.timeStopTurnConsumedFlag;
	regenerationRateMillionths = other.regenerationRateMillionths;
	regenerationPendingMicroHealth = other.regenerationPendingMicroHealth;
	summoned = other.summoned;
	natureSummoned = other.natureSummoned;
	waiting = other.waiting;
	waitedThisTurn = other.waitedThisTurn;
	battlecraftWaitBonusUsed = other.battlecraftWaitBonusUsed;
	defensiveStanceMeleeBonus = other.defensiveStanceMeleeBonus;
	defensiveStanceRangedBonus = other.defensiveStanceRangedBonus;
	bulwarkPreemptiveUsed = other.bulwarkPreemptiveUsed;
	bulwarkMireGripApplied = other.bulwarkMireGripApplied;
	bulwarkDefendPhysicalDamage = other.bulwarkDefendPhysicalDamage;
	veteranPhysicalDamageSinceActivation = other.veteranPhysicalDamageSinceActivation;
	bulwarkImmovableRound = other.bulwarkImmovableRound;
	bulwarkToxicSpinesRound = other.bulwarkToxicSpinesRound;
	physicalPoisonBaseDamage = other.physicalPoisonBaseDamage;
	physicalPoisonActivationsRemaining = other.physicalPoisonActivationsRemaining;
	physicalPoisonSourceStackId = other.physicalPoisonSourceStackId;
	guardianSpiritHitPoints = other.guardianSpiritHitPoints;
	guardianSpiritRoundsRemaining = other.guardianSpiritRoundsRemaining;
	phantomInitialIntegrity = other.phantomInitialIntegrity;
	phantomIntegrity = other.phantomIntegrity;
	phantomRoundsRemaining = other.phantomRoundsRemaining;
	phantomShadowGiftMaximumHealthLost = other.phantomShadowGiftMaximumHealthLost;
	capacityHealthReferenceMax = other.capacityHealthReferenceMax;
	battleFormOriginalHealth = other.battleFormOriginalHealth;
	battleFormCreatureId = other.battleFormCreatureId;
	battleFormOriginalCreatureId = other.battleFormOriginalCreatureId;
	battleFormRoundsRemaining = other.battleFormRoundsRemaining;
	battleFormOriginalMaxHealth = other.battleFormOriginalMaxHealth;
	battleFormOriginalCount = other.battleFormOriginalCount;
	battleFormInitiativeSnapshot = other.battleFormInitiativeSnapshot;
	battleFormInitiativeSnapshotActive = other.battleFormInitiativeSnapshotActive;
	battleFormOriginalCapacityHealthReferenceMax = other.battleFormOriginalCapacityHealthReferenceMax;
	battleFormOriginalCapacityRegenerationRemainderTenths = other.battleFormOriginalCapacityRegenerationRemainderTenths;
	casts = other.casts;
	counterAttacks = other.counterAttacks;
	shots = other.shots;
	health = other.health;
	cloneID = other.cloneID;
	position = other.position;
	if(previousEffectiveCreature != battleFormCreature()
		|| previousOriginalCreature != battleFormOriginalCreature())
		onBattleFormChanged();
	return *this;
}

int32_t CUnitState::creatureIndex() const
{
	return static_cast<int32_t>(creatureId().toEnum());
}

CreatureID CUnitState::creatureId() const
{
	return battleFormCreature();
}

int32_t CUnitState::creatureLevel() const
{
	return static_cast<int32_t>(battleFormCreature().toCreature()->getLevel());
}

bool CUnitState::doubleWide() const
{
	return battleFormCreature().toCreature()->isDoubleWide();
}

int32_t CUnitState::creatureCost() const
{
	return battleFormCreature().toCreature()->getRecruitCost(EGameResID::GOLD);
}

int32_t CUnitState::creatureIconIndex() const
{
	return battleFormCreature().toCreature()->getIconIndex();
}

FactionID CUnitState::getFactionID() const
{
	return battleFormCreature().toCreature()->getFactionID();
}

int32_t CUnitState::getCasterUnitId() const
{
	return static_cast<int32_t>(unitId());
}

const CGHeroInstance * CUnitState::getHeroCaster() const
{
	return nullptr;
}

int32_t CUnitState::getSpellSchoolLevel(const spells::Spell * spell, SpellSchool * outSelectedSchool) const
{
	int32_t skill = valOfBonuses(Selector::typeSubtype(BonusType::SPELLCASTER, BonusSubtypeID(spell->getId())));

	//Magic Plains raise level of spells cast by creatures, unlike battlefields of a specific magic school
	if(spell->getLevel() > 0)
		vstd::amax(skill, valOfBonuses(BonusType::MAGIC_SCHOOL_SKILL, BonusSubtypeID(SpellSchool::ANY)));

	vstd::abetween(skill, 0, 3);
	return skill;
}

int64_t CUnitState::getSpellBonus(const spells::Spell * spell, int64_t base, const Unit * affectedStack) const
{
	//does not have sorcery-like bonuses (yet?)
	return base;
}

int64_t CUnitState::getSpecificSpellBonus(const spells::Spell * spell, int64_t base) const
{
	return base;
}

int32_t CUnitState::getEffectLevel(const spells::Spell * spell) const
{
	return getSpellSchoolLevel(spell);
}

int32_t CUnitState::getEffectPower(const spells::Spell * spell) const
{
	return valOfBonuses(BonusType::CREATURE_SPELL_POWER) * getCount() / 100;
}

int32_t CUnitState::getEnchantPower(const spells::Spell * spell) const
{
	int32_t res = valOfBonuses(BonusType::CREATURE_ENCHANT_POWER);
	if(res <= 0)
		res = 3;//default for creatures
	return res;
}

int64_t CUnitState::getEffectValue(const spells::Spell * spell) const
{
	return static_cast<int64_t>(getCount()) * valOfBonuses(BonusType::SPECIFIC_SPELL_POWER, BonusSubtypeID(spell->getId()));
}

int64_t CUnitState::getEffectRange(const spells::Spell * spell) const
{
	return valOfBonuses(BonusType::SPECIFIC_SPELL_RANGE, BonusSubtypeID(spell->getId()));
}

PlayerColor CUnitState::getCasterOwner() const
{
	return env->unitEffectiveOwner(this);
}

std::string CUnitState::getCasterNameTextID() const
{
	const auto * creature = creatureId().toEntity(LIBRARY);
	return creature->getNamePluralTextID();
}

void CUnitState::getCastDescription(const spells::Spell * spell, const battle::Units & attacked, MetaString & text) const
{
	text.appendTextID("core.genrltxt.565");//The %s casts %s
	//todo: use text 566 for single creature
	text.replaceTextID(getCasterNameTextID());
	text.replaceName(spell->getId());
}

int32_t CUnitState::manaLimit() const
{
	return 0; //TODO: creature casting with mana mode (for mods)
}

bool CUnitState::ableToRetaliate() const
{
	return alive()
		&& !isTimeStopped()
		&& counterAttacks.canUse();
}

bool CUnitState::alive() const
{
	return health.getCount() > 0;
}

bool CUnitState::isGhost() const
{
	return ghost;
}

bool CUnitState::isFrozen() const
{
	return hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::STONE_GAZE))));
}

bool CUnitState::isValidTarget(bool allowDead) const
{
	return !isTimeStopped()
		&& (alive() || (allowDead && isDead())) && getPosition().isValid() && !isTurret();
}

bool CUnitState::isClone() const
{
	return cloned;
}

bool CUnitState::hasClone() const
{
	return cloneID > 0;
}

bool CUnitState::canCast() const
{
	return casts.canUse(1) && !castSpellThisTurn;//do not check specific cast abilities here
}

bool CUnitState::isCaster() const
{
	return casts.total() > 0;//do not check specific cast abilities here
}

bool CUnitState::canShootBlocked() const
{
	return bonusCache.hasBonus(UnitBonusValuesProxy::HAS_FREE_SHOOTING);
}

bool CUnitState::canShoot() const
{
	return
		shots.canUse(1) &&
		bonusCache.getBonusValue(UnitBonusValuesProxy::FORGETFULL) < 100; //100% forgetfulness disables shooting
}

bool CUnitState::isShooter() const
{
	return shots.total() > 0;
}

int32_t CUnitState::getKilled() const
{
	const auto & provenanceHealth = battleFormOriginalHealth.isBattleFormProvenance()
		? battleFormOriginalHealth
		: health;
	int32_t res = unitBaseAmount() - provenanceHealth.getCount() + provenanceHealth.getResurrected();
	vstd::amax(res, 0);
	return res;
}

int32_t CUnitState::getCount() const
{
	return health.getCount();
}

int32_t CUnitState::getFirstHPleft() const
{
	return health.getFirstHPleft();
}

int32_t CUnitState::getUnusableRemains() const
{
	return battleFormOriginalHealth.isBattleFormProvenance()
		? battleFormOriginalHealth.getUnusableRemains()
		: health.getUnusableRemains();
}

int64_t CUnitState::getAvailableHealth() const
{
	if(phantomInitialIntegrity > 0)
		return phantomIntegrity;

	return health.available();
}

int64_t CUnitState::getSurvivingMissingHealth() const
{
	if(phantomInitialIntegrity > 0 || health.getCount() <= 0)
		return 0;
	const int64_t survivorCapacity = static_cast<int64_t>(health.getCount()) * getMaxHealth();
	return std::max<int64_t>(0, survivorCapacity - health.getCreatureHealthAvailable());
}

int64_t CUnitState::getTotalHealth() const
{
	return battleFormOriginalHealth.isBattleFormProvenance()
		? battleFormOriginalHealth.total()
		: health.total();
}

int64_t CUnitState::getShadowGiftCurrentHealth() const
{
	return phantomInitialIntegrity > 0 ? phantomIntegrity : health.getCreatureHealthAvailable();
}

int64_t CUnitState::getShadowGiftMaximumHealth() const
{
	return phantomInitialIntegrity > 0
		? std::max<int64_t>(0, phantomInitialIntegrity - phantomShadowGiftMaximumHealthLost)
		: (battleFormOriginalHealth.isBattleFormProvenance()
			? battleFormOriginalHealth.total()
			: health.total());
}

int64_t CUnitState::getShadowGiftMaximumHealthLost() const
{
	return (battleFormOriginalHealth.isBattleFormProvenance()
		? battleFormOriginalHealth.getShadowGiftMaximumHealthLost()
		: health.getShadowGiftMaximumHealthLost()) + phantomShadowGiftMaximumHealthLost;
}

void CUnitState::addShadowGiftMaximumHealthLoss(const int64_t amount)
{
	if(amount < 0)
		throw std::invalid_argument("Negative Shadow Gift maximum-health loss");
	if(phantomInitialIntegrity > 0)
	{
		const int64_t effectiveMaximum = getShadowGiftMaximumHealth();
		const int64_t actualLoss = std::min(amount, effectiveMaximum);
		phantomShadowGiftMaximumHealthLost += actualLoss;
		return;
	}
	if(battleFormOriginalHealth.isBattleFormProvenance())
	{
		const int64_t oldLoss = battleFormOriginalHealth.getShadowGiftMaximumHealthLost();
		battleFormOriginalHealth.addShadowGiftMaximumHealthLoss(amount);
		const int64_t addedLoss = battleFormOriginalHealth.getShadowGiftMaximumHealthLost() - oldLoss;
		if(addedLoss > 0)
			health.addShadowGiftMaximumHealthLoss(addedLoss);
	}
	else
	{
		health.addShadowGiftMaximumHealthLoss(amount);
	}
}

int64_t CUnitState::getPhantomIntegrity() const
{
	return phantomIntegrity;
}

int64_t CUnitState::getPhantomInitialIntegrity() const
{
	return phantomInitialIntegrity;
}

int64_t CUnitState::getGuardianSpiritHitPoints() const
{
	return guardianSpiritHitPoints;
}

int32_t CUnitState::getGuardianSpiritRoundsRemaining() const
{
	return guardianSpiritRoundsRemaining;
}

void CUnitState::initializePhantomProfile(int64_t integrity, int32_t duration)
{
	if(phantomInitialIntegrity != 0 || phantomIntegrity != 0 || phantomRoundsRemaining != 0)
		throw std::logic_error("Phantom Army profile is already initialized");
	if(integrity <= 0
		|| !newHorizonsSorcery::phantomArmyDurationSupported(duration)
		|| !summoned || natureSummoned || cloned || getCount() <= 0)
		throw std::invalid_argument("Invalid Phantom Army profile");

	phantomInitialIntegrity = integrity;
	phantomIntegrity = integrity;
	phantomRoundsRemaining = duration;
}

bool CUnitState::hasBattleForm() const
{
	return battleFormRoundsRemaining > 0 && battleFormCreatureId.hasValue();
}

CreatureID CUnitState::battleFormCreature() const
{
	if(hasBattleForm())
		return battleFormCreatureId;
	if(battleFormOriginalCreatureId.hasValue())
		return battleFormOriginalCreatureId;
	return unitType()->getId();
}

CreatureID CUnitState::battleFormOriginalCreature() const
{
	if(battleFormOriginalCreatureId.hasValue())
		return battleFormOriginalCreatureId;
	return unitType()->getId();
}

bool CUnitState::hasBattleFormState() const
{
	return hasBattleForm()
		|| battleFormCreatureId.hasValue()
		|| battleFormOriginalCreatureId.hasValue()
		|| battleFormOriginalHealth.isBattleFormProvenance()
		|| battleFormRoundsRemaining != 0
		|| battleFormOriginalMaxHealth != 0
		|| battleFormOriginalCount != 0;
}

void CUnitState::beginBattleForm(const CreatureID creature, const int32_t rounds)
{
	if(rounds <= 0 || !creature.hasValue() || creature.toEntity(LIBRARY) == nullptr)
		throw std::invalid_argument("Invalid battle-form target or duration");
	if(phantomInitialIntegrity > 0 || !alive())
		throw std::logic_error("This unit cannot receive a battle form");

	const CreatureID previousForm = battleFormCreature();
	const bool wasActive = hasBattleForm();
	const int32_t currentInitiative = getInitiative(0);
	const int64_t currentCreatureHealth = health.getCreatureHealthAvailable();
	if(!battleFormOriginalHealth.isBattleFormProvenance())
	{
		if(!battleFormOriginalCreatureId.hasValue())
			battleFormOriginalCreatureId = unitType()->getId();
		battleFormOriginalMaxHealth = static_cast<int32_t>(getMaxHealth());
		battleFormOriginalCount = unitBaseAmount();
		battleFormOriginalCapacityHealthReferenceMax = capacityHealthReferenceMax;
		battleFormOriginalCapacityRegenerationRemainderTenths = capacityRegenerationRemainderTenths;
		battleFormOriginalHealth = health;
		battleFormOriginalHealth.setTemporaryHitPoints(0);
		battleFormOriginalHealth.preserveBattleFormProvenance(battleFormOriginalMaxHealth);
	}

	if(health.getCreatureHealthAvailable() != battleFormOriginalHealth.getCreatureHealthAvailable())
		throw std::logic_error("Battle-form HP diverged from source-species provenance");

	battleFormCreatureId = creature;
	battleFormRoundsRemaining = rounds;
	battleFormInitiativeSnapshot = currentInitiative;
	battleFormInitiativeSnapshotActive = true;
	capacityHealthReferenceMax = 0;
	capacityRegenerationRemainderTenths = 0;
	if(!wasActive || previousForm != creature)
	{
		onBattleFormChanged();
		health.repartitionForBattleForm(static_cast<int32_t>(getMaxHealth()),
			battleFormOriginalHealth.total(), currentCreatureHealth);
	}
}

void CUnitState::endBattleForm()
{
	if(!hasBattleForm())
		return;
	if(!battleFormOriginalHealth.isBattleFormProvenance())
		throw std::logic_error("Cannot restore a battle form without source HP provenance");
	if(health.getCreatureHealthAvailable() != battleFormOriginalHealth.getCreatureHealthAvailable())
		throw std::logic_error("Battle-form HP diverged from source-species provenance");

	const CreatureID activeCreature = battleFormCreatureId;
	const int32_t activeRoundsRemaining = battleFormRoundsRemaining;
	const int32_t activeInitiativeSnapshot = battleFormInitiativeSnapshot;
	const bool activeInitiativeSnapshotEnabled = battleFormInitiativeSnapshotActive;
	const int64_t temporaryHitPoints = health.getTemporaryHitPoints();
	battleFormCreatureId = CreatureID(-1);
	battleFormRoundsRemaining = 0;
	battleFormInitiativeSnapshot = 0;
	battleFormInitiativeSnapshotActive = false;
	onBattleFormChanged();

	CHealth restoredHealth = battleFormOriginalHealth;
	restoredHealth.setTemporaryHitPoints(temporaryHitPoints);
	restoredHealth.releaseBattleFormProvenance(battleFormOriginalCapacityHealthReferenceMax > 0);
	if(restoredHealth.getCreatureHealthAvailable() != battleFormOriginalHealth.getCreatureHealthAvailable())
	{
		battleFormCreatureId = activeCreature;
		battleFormRoundsRemaining = activeRoundsRemaining;
		battleFormInitiativeSnapshot = activeInitiativeSnapshot;
		battleFormInitiativeSnapshotActive = activeInitiativeSnapshotEnabled;
		onBattleFormChanged();
		throw std::logic_error("Restoring a battle form changed source-species HP");
	}

	health = restoredHealth;
	capacityHealthReferenceMax = battleFormOriginalCapacityHealthReferenceMax;
	capacityRegenerationRemainderTenths = battleFormOriginalCapacityRegenerationRemainderTenths;
	battleFormOriginalHealth.reset();
	battleFormOriginalMaxHealth = 0;
	battleFormOriginalCount = 0;
	battleFormOriginalCapacityHealthReferenceMax = 0;
	battleFormOriginalCapacityRegenerationRemainderTenths = 0;
}

void CUnitState::onBattleFormChanged()
{
	++battleFormViewRevision;
}

int32_t CUnitState::getBattleFormViewRevision() const
{
	return battleFormViewRevision;
}

uint32_t CUnitState::getMaxHealth() const
{
	return std::max(1, bonusCache.getBonusValue(UnitBonusValuesProxy::STACK_HEALTH));
}

BattleHex CUnitState::getPosition() const
{
	return position;
}

void CUnitState::setPosition(const BattleHex & hex)
{
	position = hex;
}

int32_t CUnitState::getInitiative(int turn) const
{
	if(turn == 0 && hasBattleForm() && battleFormInitiativeSnapshotActive)
		return battleFormInitiativeSnapshot;
	const int64_t speed = stackSpeedPerTurn.getValue(turn) + (turn == 0 && env ? env->unitFortuneSpeed(this) : 0);
	const int64_t baseInitiative = initiativeBasePresencePerTurn.getValue(turn)
		? initiativeBasePerTurn.getValue(turn)
		: speed;
	const int64_t percent = std::max<int64_t>(0, 100 + initiativePercentPerTurn.getValue(turn));
	return static_cast<int32_t>(baseInitiative * percent / 100 + initiativeFlatPerTurn.getValue(turn));
}

ui32 CUnitState::getMovementRange(int turn) const
{
	if(isTimeStopped())
		return 0;

	if (immobilizedPerTurn.getValue(0) != 0)
		return 0;

	const int64_t movementRange = stackSpeedPerTurn.getValue(0) + movementRangePerTurn.getValue(0)
		+ (env ? env->unitFortuneSpeed(this) : 0)
		+ (turn == 0 ? activationMovementBonus : 0);
	return static_cast<ui32>(std::max<int64_t>(0, movementRange));
}

void CUnitState::setActivationMovementBonus(int32_t value)
{
	if(value < 0)
		throw std::runtime_error("Invalid negative activation movement bonus");
	activationMovementBonus = value;
}

void CUnitState::setRangedFollowUpDamagePercent(int32_t value)
{
	if(value < 0 || value > 100)
		throw std::runtime_error("Invalid ranged follow-up damage percentage");
	rangedFollowUpDamagePercent = value;
}

ui32 CUnitState::getMovementRange() const
{
	return getMovementRange(0);
}

uint8_t CUnitState::getRangedFullDamageDistance() const
{
	if(!isShooter())
		return 0;

	// overwrite full ranged damage distance with the value set in Additional info field of LIMITED_SHOOTING_RANGE bonus
	if(hasBonusOfType(BonusType::LIMITED_SHOOTING_RANGE))
	{
		auto bonus = this->getBonus(Selector::type()(BonusType::LIMITED_SHOOTING_RANGE));
		if(bonus != nullptr && bonus->parameters)
			return bonus->parameters->toNumber();
	}

	if (hasBonusOfType(BonusType::NO_DISTANCE_PENALTY))
		return GameConstants::BATTLE_SHOOTING_RANGE_DISTANCE;

	return GameConstants::BATTLE_SHOOTING_PENALTY_DISTANCE;
}

uint8_t CUnitState::getShootingRangeDistance() const
{
	if(!isShooter())
		return 0;

	uint8_t shootingRangeDistance = GameConstants::BATTLE_SHOOTING_RANGE_DISTANCE;

	// overwrite full ranged damage distance with the value set in Additional info field of LIMITED_SHOOTING_RANGE bonus
	if(hasBonusOfType(BonusType::LIMITED_SHOOTING_RANGE))
	{
		auto bonus = this->getBonus(Selector::type()(BonusType::LIMITED_SHOOTING_RANGE));
		if(bonus != nullptr)
			shootingRangeDistance = bonus->val;
	}

	return shootingRangeDistance;
}

bool CUnitState::canMove(int turn) const
{
	if (!alive())
		return false;
	if(isTimeStopped())
		return false;

	if (turn == 0)
		return !hasBonusOfType(BonusType::NOT_ACTIVE);

	std::string cachingStr = "type_NOT_ACTIVE_turns_" + std::to_string(turn);
	return !hasBonus(Selector::type()(BonusType::NOT_ACTIVE).And(Selector::turns(turn)), cachingStr); //eg. Ammo Cart or blinded creature
}

bool CUnitState::defended(int turn) const
{
	return !turn && defending;
}

bool CUnitState::moved(int turn) const
{
	if(!turn && !waiting)
		return movedThisRound;
	else
		return false;
}

bool CUnitState::timeStopTurnConsumed() const
{
	return timeStopTurnConsumedFlag;
}

bool CUnitState::willMove(int turn) const
{
	return (turn ? true : !defending)
		   && !moved(turn)
		   && canMove(turn);
}

bool CUnitState::waited(int turn) const
{
	if(!turn)
		return waiting;
	else
		return false;
}

bool CUnitState::battlecraftWaitBonusAvailable() const
{
	return waitedThisTurn && !battlecraftWaitBonusUsed;
}

BattlePhases::Type CUnitState::battleQueuePhase(int turn) const
{
	if(turn <= 0 && waited()) //consider waiting state only for ongoing round
	{
		if(hadMorale)
			return BattlePhases::WAIT_MORALE;
		else
			return BattlePhases::WAIT;
	}
	else if(isCatapult() || isTurret()) //catapult and turrets are first
	{
		return BattlePhases::SIEGE;
	}
	else
	{
		return BattlePhases::NORMAL;
	}
}

bool CUnitState::isHypnotized() const
{
	return bonusCache.hasBonus(UnitBonusValuesProxy::HYPNOTIZED);
}

bool CUnitState::isInvincible() const
{
	return bonusCache.hasBonus(UnitBonusValuesProxy::INVINCIBLE);
}

bool CUnitState::isTimeStopped() const
{
	if(hasBonusOfType(BonusType::TIME_STOP))
		return true;

	// Compatibility with the original data-only fallback marker.  New saves
	// use the dedicated TIME_STOP bonus, but an in-flight legacy battle may
	// still carry a hidden NONE marker from the Lua implementation.
	const auto effects = getBonuses(Selector::sourceType()(BonusSource::SPELL_EFFECT));
	return vstd::contains_if(*effects, [](const std::shared_ptr<Bonus> & bonus)
	{
		if(!bonus || bonus->type != BonusType::NONE || !bonus->sid.as<SpellID>().hasValue())
			return false;
		const auto * spell = bonus->sid.as<SpellID>().toSpell();
		return spell && spell->getJsonKey() == newHorizonsSorcery::TIME_STOP_SPELL;
	});
}

int CUnitState::getTotalAttacks(bool ranged) const
{
	return 1 + (ranged ?
		bonusCache.getBonusValue(UnitBonusValuesProxy::TOTAL_ATTACKS_RANGED):
		bonusCache.getBonusValue(UnitBonusValuesProxy::TOTAL_ATTACKS_MELEE));
}

int CUnitState::getMinDamage(bool ranged) const
{
	return ranged ?
		bonusCache.getBonusValue(UnitBonusValuesProxy::MIN_DAMAGE_RANGED):
		bonusCache.getBonusValue(UnitBonusValuesProxy::MIN_DAMAGE_MELEE);

}

int CUnitState::getMaxDamage(bool ranged) const
{
	return ranged ?
		bonusCache.getBonusValue(UnitBonusValuesProxy::MAX_DAMAGE_RANGED):
		bonusCache.getBonusValue(UnitBonusValuesProxy::MAX_DAMAGE_MELEE);
}

int CUnitState::getAttack(bool ranged) const
{
	int attack = ranged ?
		bonusCache.getBonusValue(UnitBonusValuesProxy::ATTACK_RANGED):
		bonusCache.getBonusValue(UnitBonusValuesProxy::ATTACK_MELEE);

	vstd::amax(attack, 0);
	return attack;
}

int CUnitState::getDefense(bool ranged) const
{
	int defence = ranged ?
					  bonusCache.getBonusValue(UnitBonusValuesProxy::DEFENCE_RANGED):
					  bonusCache.getBonusValue(UnitBonusValuesProxy::DEFENCE_MELEE);

	vstd::amax(defence, 0);
	return defence;
}

int CUnitState::getDefenseIgnoringDefensiveStance(bool ranged) const
{
	const int stanceBonus = ranged ? defensiveStanceRangedBonus : defensiveStanceMeleeBonus;
	return std::max(0, getDefense(ranged) - (defending ? stanceBonus : 0));
}

std::shared_ptr<Unit> CUnitState::acquire() const
{
	auto ret = std::make_shared<CUnitStateDetached>(this, this);
	ret->localInit(env);
	*ret = *this;
	return ret;
}

std::shared_ptr<CUnitState> CUnitState::acquireState() const
{
	auto ret = std::make_shared<CUnitStateDetached>(this, this);
	ret->localInit(env);
	*ret = *this;
	return ret;
}

void CUnitState::serializeJson(JsonSerializeFormat & handler)
{
	handler.serializeBool("cloned", cloned);
	handler.serializeBool("defending", defending);
	handler.serializeBool("drainedMana", drainedMana);
	handler.serializeBool("fear", fear);
	handler.serializeBool("hadMorale", hadMorale);
	handler.serializeBool("castSpellThisTurn", castSpellThisTurn);
	handler.serializeBool("ghost", ghost);
	handler.serializeBool("ghostPending", ghostPending);
	handler.serializeBool("moved", movedThisRound);
	handler.serializeInt("pursuitMovementRemaining", pursuitMovementRemaining, 0);
	if(pursuitMovementRemaining < 0)
		throw std::runtime_error("Invalid negative Pursuit movement allowance");
	handler.serializeBool("cleaveUsedThisActivation", cleaveUsedThisActivation);
	handler.serializeInt("rangedFollowUpDamagePercent", rangedFollowUpDamagePercent, 0);
	if(rangedFollowUpDamagePercent < 0 || rangedFollowUpDamagePercent > 100)
		throw std::runtime_error("Invalid saved ranged follow-up state");
	handler.serializeInt("archeryCounterfireRound", archeryCounterfireRound, -1);
	handler.serializeInt("archeryDeadeyeRound", archeryDeadeyeRound, -1);
	handler.serializeInt("archerySuppressionActivationSerial", archerySuppressionActivationSerial, -1);
	handler.serializeInt("archeryRainOfArrowsActivationSerial", archeryRainOfArrowsActivationSerial, -1);
	handler.serializeInt("archeryCrossfireRound", archeryCrossfireRound, -1);
	handler.enterArray("archeryCrossfireAttackers").serializeArray(archeryCrossfireAttackers);
	handler.enterArray("archeryCrossfireDefenders").serializeArray(archeryCrossfireDefenders);
	handler.serializeInt("noQuarterMoraleActivationsRemaining", noQuarterMoraleActivationsRemaining, 0);
	if(noQuarterMoraleActivationsRemaining < 0 || noQuarterMoraleActivationsRemaining > 2)
		throw std::runtime_error("Invalid No Quarter morale lifetime");
	handler.serializeInt("capacityRegenerationRemainderTenths", capacityRegenerationRemainderTenths, 0);
	if(capacityRegenerationRemainderTenths < 0 || capacityRegenerationRemainderTenths > 9)
		throw std::runtime_error("Invalid capacity regeneration remainder");
	handler.serializeBool("timeStopTurnConsumed", timeStopTurnConsumedFlag);
	handler.serializeInt("regenerationRateMillionths", regenerationRateMillionths, 0);
	handler.serializeInt("regenerationPendingMicroHealth", regenerationPendingMicroHealth, 0);
	if(regenerationRateMillionths < 0
		|| regenerationRateMillionths > newHorizonsMagic::REGENERATION_MAX_RATE_MILLIONTHS
		|| regenerationPendingMicroHealth < 0)
		throw std::runtime_error("Invalid saved Regeneration state");
	handler.serializeBool("summoned", summoned);
	handler.serializeBool("natureSummoned", natureSummoned);
	handler.serializeBool("waiting", waiting);
	handler.serializeBool("waitedThisTurn", waitedThisTurn);
	handler.serializeBool("battlecraftWaitBonusUsed", battlecraftWaitBonusUsed);
	handler.serializeInt("activationMovementBonus", activationMovementBonus, 0);
	if(activationMovementBonus < 0)
		throw std::runtime_error("Invalid negative activation movement bonus");
	handler.serializeInt("defensiveStanceMeleeBonus", defensiveStanceMeleeBonus, 0);
	handler.serializeInt("defensiveStanceRangedBonus", defensiveStanceRangedBonus, 0);
	handler.serializeBool("bulwarkPreemptiveUsed", bulwarkPreemptiveUsed);
	handler.serializeBool("bulwarkMireGripApplied", bulwarkMireGripApplied);
	handler.serializeInt("bulwarkDefendPhysicalDamage", bulwarkDefendPhysicalDamage, 0);
	if(bulwarkDefendPhysicalDamage < 0)
		throw std::runtime_error("Invalid negative Bulwark damage accumulator");
	handler.serializeInt("veteranPhysicalDamageSinceActivation", veteranPhysicalDamageSinceActivation, 0);
	if(veteranPhysicalDamageSinceActivation < 0)
		throw std::runtime_error("Invalid negative Veteran damage accumulator");
	handler.serializeInt("bulwarkImmovableRound", bulwarkImmovableRound, -1);
	if(bulwarkImmovableRound < -1)
		throw std::runtime_error("Invalid Bulwark perk round marker");
	handler.serializeInt("bulwarkToxicSpinesRound", bulwarkToxicSpinesRound, -1);
	if(bulwarkToxicSpinesRound < -1)
		throw std::runtime_error("Invalid Toxic Spines round marker");
	handler.serializeInt("physicalPoisonBaseDamage", physicalPoisonBaseDamage, 0);
	handler.serializeInt("physicalPoisonActivationsRemaining", physicalPoisonActivationsRemaining, 0);
	handler.serializeInt("physicalPoisonSourceStackId", physicalPoisonSourceStackId, -1);
	if(physicalPoisonBaseDamage < 0 || physicalPoisonActivationsRemaining < 0
		|| physicalPoisonActivationsRemaining > 3
		|| ((physicalPoisonBaseDamage == 0) != (physicalPoisonActivationsRemaining == 0)))
		throw std::runtime_error("Invalid physical Poison state");
	handler.serializeInt("guardianSpiritHitPoints", guardianSpiritHitPoints, 0);
	handler.serializeInt("guardianSpiritRoundsRemaining", guardianSpiritRoundsRemaining, 0);
	if(guardianSpiritHitPoints < 0 || guardianSpiritRoundsRemaining < 0
		|| ((guardianSpiritHitPoints == 0) != (guardianSpiritRoundsRemaining == 0)))
		throw std::runtime_error("Invalid Guardian Spirit state");
	handler.serializeInt("phantomInitialIntegrity", phantomInitialIntegrity, 0);
	handler.serializeInt("phantomIntegrity", phantomIntegrity, 0);
	handler.serializeInt("phantomRoundsRemaining", phantomRoundsRemaining, 0);
	handler.serializeInt("phantomShadowGiftMaximumHealthLost", phantomShadowGiftMaximumHealthLost, 0);
	if(phantomShadowGiftMaximumHealthLost < 0
		|| phantomShadowGiftMaximumHealthLost > phantomInitialIntegrity)
		throw std::runtime_error("Invalid Phantom Army Shadow Gift cap loss");

	handler.serializeStruct("casts", casts);
	handler.serializeStruct("counterAttacks", counterAttacks);
	handler.serializeStruct("shots", shots);
	handler.serializeInt("capacityHealthReferenceMax", capacityHealthReferenceMax, 0);
	handler.serializeInt("cloneID", cloneID);
	handler.serializeId("battleFormCreature", battleFormCreatureId, CreatureID(-1));
	handler.serializeId("battleFormOriginalCreature", battleFormOriginalCreatureId, CreatureID(-1));
	handler.serializeInt("battleFormRoundsRemaining", battleFormRoundsRemaining, 0);
	handler.serializeInt("battleFormOriginalMaxHealth", battleFormOriginalMaxHealth, 0);
	handler.serializeInt("battleFormOriginalCount", battleFormOriginalCount, 0);
	handler.serializeInt("battleFormInitiativeSnapshot", battleFormInitiativeSnapshot, 0);
	handler.serializeBool("battleFormInitiativeSnapshotActive", battleFormInitiativeSnapshotActive, false);
	handler.serializeInt("battleFormOriginalCapacityHealthReferenceMax", battleFormOriginalCapacityHealthReferenceMax, 0);
	handler.serializeInt("battleFormOriginalCapacityRegenerationRemainderTenths", battleFormOriginalCapacityRegenerationRemainderTenths, 0);
	handler.serializeStruct("battleFormOriginalHealth", battleFormOriginalHealth);
	if(!handler.saving && hasBattleFormState())
		onBattleFormChanged();
	handler.serializeStruct("health", health);
	if(capacityHealthReferenceMax < 0
		|| (capacityHealthReferenceMax > 0 && !health.isCapacityHealthTracking())
		|| (capacityHealthReferenceMax == 0 && capacityRegenerationRemainderTenths != 0))
		throw std::runtime_error("Invalid capacity health reference state");
	if(battleFormRoundsRemaining < 0 || battleFormOriginalMaxHealth < 0 || battleFormOriginalCount < 0
		|| battleFormOriginalCapacityHealthReferenceMax < 0
		|| battleFormOriginalCapacityRegenerationRemainderTenths < 0
		|| battleFormOriginalCapacityRegenerationRemainderTenths > 9)
		throw std::runtime_error("Invalid battle-form metadata");
	if(hasBattleForm())
	{
		if(!battleFormOriginalCreatureId.hasValue()
			|| battleFormOriginalMaxHealth <= 0 || battleFormOriginalCount != unitBaseAmount()
			|| !battleFormOriginalHealth.isBattleFormProvenance()
			|| battleFormOriginalHealth.getTemporaryHitPoints() != 0
			|| capacityHealthReferenceMax != 0 || capacityRegenerationRemainderTenths != 0
			|| health.total() != battleFormOriginalHealth.total()
			|| health.getCreatureHealthAvailable() != battleFormOriginalHealth.getCreatureHealthAvailable())
			throw std::runtime_error("Invalid active battle-form health state");
	}
	else if(battleFormCreatureId.hasValue() || battleFormRoundsRemaining != 0
		|| battleFormInitiativeSnapshotActive
		|| battleFormOriginalMaxHealth != 0 || battleFormOriginalCount != 0
		|| battleFormOriginalCapacityHealthReferenceMax != 0
		|| battleFormOriginalCapacityRegenerationRemainderTenths != 0
		|| battleFormOriginalHealth.isBattleFormProvenance()
		|| health.getTotalHealthOverride() != 0)
		throw std::runtime_error("Invalid inactive battle-form state");

	si16 posValue = position.toInt();
	handler.serializeInt("position", posValue);
	position = posValue;
}

void CUnitState::localInit(const IUnitEnvironment * env_)
{
	env = env_;

	shots.setEnv(env);
	reset();
	health.init();
}

void CUnitState::reset()
{
	cloned = false;
	activationMovementBonus = 0;
	defending = false;
	drainedMana = false;
	fear = false;
	hadMorale = false;
	castSpellThisTurn = false;
	ghost = false;
	ghostPending = false;
	movedThisRound = false;
	timeStopTurnConsumedFlag = false;
	rangedFollowUpDamagePercent = 0;
	regenerationRateMillionths = 0;
	regenerationPendingMicroHealth = 0;
	noQuarterMoraleActivationsRemaining = 0;
	capacityRegenerationRemainderTenths = 0;
	summoned = false;
	natureSummoned = false;
	waiting = false;
	waitedThisTurn = false;
	battlecraftWaitBonusUsed = false;
	defensiveStanceMeleeBonus = 0;
	defensiveStanceRangedBonus = 0;
	bulwarkPreemptiveUsed = false;
	bulwarkMireGripApplied = false;
	bulwarkDefendPhysicalDamage = 0;
	veteranPhysicalDamageSinceActivation = 0;
	bulwarkImmovableRound = -1;
	bulwarkToxicSpinesRound = -1;
	physicalPoisonBaseDamage = 0;
	physicalPoisonActivationsRemaining = 0;
	physicalPoisonSourceStackId = -1;
	guardianSpiritHitPoints = 0;
	guardianSpiritRoundsRemaining = 0;
	phantomInitialIntegrity = 0;
	phantomIntegrity = 0;
	phantomRoundsRemaining = 0;
	phantomShadowGiftMaximumHealthLost = 0;
	capacityHealthReferenceMax = 0;
	battleFormOriginalHealth.reset();
	battleFormCreatureId = CreatureID(-1);
	battleFormOriginalCreatureId = CreatureID(-1);
	battleFormRoundsRemaining = 0;
	battleFormOriginalMaxHealth = 0;
	battleFormOriginalCount = 0;
	battleFormInitiativeSnapshot = 0;
	battleFormInitiativeSnapshotActive = false;
	battleFormOriginalCapacityHealthReferenceMax = 0;
	battleFormOriginalCapacityRegenerationRemainderTenths = 0;

	casts.reset();
	counterAttacks.reset();
	health.reset();
	shots.reset();

	cloneID = -1;

	position = BattleHex::INVALID;
	archeryCounterfireRound = -1;
	archeryDeadeyeRound = -1;
	archerySuppressionActivationSerial = -1;
	archeryRainOfArrowsActivationSerial = -1;
	archeryCrossfireRound = -1;
	archeryCrossfireAttackers.clear();
	archeryCrossfireDefenders.clear();
}

bool CUnitState::archeryCrossfireAvailable(BattleSide side, uint32_t currentShooter, int32_t round) const
{
	if(archeryCrossfireRound != round || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return false;
	const auto & shooters = side == BattleSide::ATTACKER ? archeryCrossfireAttackers : archeryCrossfireDefenders;
	return std::ranges::any_of(shooters, [currentShooter](uint32_t shooter)
	{
		return shooter != currentShooter;
	});
}

void CUnitState::archeryRecordCrossfireDamage(BattleSide side, uint32_t shooter, int32_t round)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return;
	if(archeryCrossfireRound != round)
	{
		archeryCrossfireRound = round;
		archeryCrossfireAttackers.clear();
		archeryCrossfireDefenders.clear();
	}
	auto * shooters = side == BattleSide::ATTACKER ? &archeryCrossfireAttackers : &archeryCrossfireDefenders;
	if(std::ranges::find(*shooters, shooter) == shooters->end())
		shooters->push_back(shooter);
}

JsonNode CUnitState::save()
{
	JsonNode data;
	//TODO: use instance resolver
	JsonSerializer ser(nullptr, data);
	ser.serializeStruct("state", *this);
	return data;
}

void CUnitState::load(const JsonNode & data)
{
	//TODO: use instance resolver
	const bool previousBattleFormState = hasBattleFormState();
	reset();
	if(previousBattleFormState)
		onBattleFormChanged();
	JsonDeserializer deser(nullptr, data);
	deser.serializeStruct("state", *this);
	if(phantomInitialIntegrity < 0 || phantomIntegrity < 0 || phantomRoundsRemaining < 0
		|| guardianSpiritHitPoints < 0 || guardianSpiritRoundsRemaining < 0
		|| ((guardianSpiritHitPoints == 0) != (guardianSpiritRoundsRemaining == 0))
		|| regenerationRateMillionths < 0
		|| regenerationRateMillionths > newHorizonsMagic::REGENERATION_MAX_RATE_MILLIONTHS
		|| regenerationPendingMicroHealth < 0
		|| phantomIntegrity > phantomInitialIntegrity - phantomShadowGiftMaximumHealthLost
		|| phantomRoundsRemaining > newHorizonsSorcery::PHANTOM_ARMY_MAX_DURATION_ROUNDS
		|| (phantomInitialIntegrity == 0 && (phantomIntegrity != 0 || phantomRoundsRemaining != 0))
		|| (phantomInitialIntegrity > 0 && (!summoned || natureSummoned || cloned))
		|| (phantomInitialIntegrity > 0 && ((phantomIntegrity > 0
			&& (phantomRoundsRemaining == 0 || !alive() || health.getCount() != unitBaseAmount()))
			|| (phantomIntegrity == 0 && (phantomRoundsRemaining != 0 || alive())))))
		throw std::runtime_error("Invalid saved Phantom Army profile");
}

void CUnitState::damage(int64_t & amount)
{
	damage(amount, false);
}

void CUnitState::damage(int64_t & amount, bool destroyRemains)
{
	damageInternal(amount, destroyRemains, false, DamageProvenance::OTHER);
}

void CUnitState::damage(int64_t & amount, bool destroyRemains, DamageProvenance provenance)
{
	damageInternal(amount, destroyRemains, false, provenance);
}

void CUnitState::damageShadowGiftSacrifice(int64_t & amount)
{
	damageInternal(amount, false, true, DamageProvenance::OTHER);
}

void CUnitState::damageInternal(int64_t & amount, bool destroyRemains, bool bypassTemporaryHitPoints,
	DamageProvenance provenance)
{
	if(isTimeStopped())
	{
		amount = 0;
		return;
	}
	const int32_t firstHPleftBefore = health.getFirstHPleft();
	const int32_t countBefore = health.getCount();
	const int32_t maximumCreatureHealth = getMaxHealth();
	if(amount > 0 && provenance == DamageProvenance::PHYSICAL_CREATURE
		&& guardianSpiritHitPoints > 0 && guardianSpiritRoundsRemaining > 0)
	{
		const auto absorbed = std::min(amount, guardianSpiritHitPoints);
		amount -= absorbed;
		guardianSpiritHitPoints -= absorbed;
		if(guardianSpiritHitPoints == 0)
			guardianSpiritRoundsRemaining = 0;
	}

	if(cloned)
	{
		// block ability should not kill clone (0 damage)
		if(amount > 0)
		{
			amount = 0;
			// A clone still dies to the first positive hit. Restore its source form
			// before clearing health so a temporary form cannot retain a stale HP
			// provenance ledger through ghosting, removal, or result processing.
			endBattleForm();
			health.reset();
		}
	}
	else if(phantomInitialIntegrity > 0)
	{
		amount = std::clamp<int64_t>(amount, 0, phantomIntegrity);
		phantomIntegrity -= amount;
		if(phantomIntegrity == 0)
		{
			phantomRoundsRemaining = 0;
			health.reset();
			ghostPending = true;
		}
	}
	else
	{
		const int64_t creatureHealthBefore = health.getCreatureHealthAvailable();
		const bool activeBattleForm = battleFormOriginalHealth.isBattleFormProvenance();
		health.damage(amount, activeBattleForm ? false : destroyRemains, bypassTemporaryHitPoints);
		normalizeCapacityHealth();
		const int64_t creatureHealthAfter = health.getCreatureHealthAvailable();
		if(activeBattleForm)
		{
			if(creatureHealthAfter < creatureHealthBefore)
			{
				int64_t provenanceDamage = creatureHealthBefore - creatureHealthAfter;
				battleFormOriginalHealth.damage(provenanceDamage, destroyRemains, true);
				if(provenanceDamage != creatureHealthBefore - creatureHealthAfter)
					throw std::logic_error("Battle-form source HP provenance rejected applied damage");
			}
			else if(creatureHealthAfter > creatureHealthBefore)
			{
				throw std::logic_error("Damage resolution unexpectedly increased creature HP");
			}
		}
		if(provenance == DamageProvenance::PHYSICAL_CREATURE && creatureHealthAfter < creatureHealthBefore)
		{
			const int64_t actualCreatureDamage = creatureHealthBefore - creatureHealthAfter;
			const auto maximum = std::numeric_limits<int64_t>::max();
			if(veteranPhysicalDamageSinceActivation > maximum - actualCreatureDamage)
				veteranPhysicalDamageSinceActivation = maximum;
			else
				veteranPhysicalDamageSinceActivation += actualCreatureDamage;
		}
	}

	bool disintegrate = hasBonusOfType(BonusType::DISINTEGRATE);
	if(health.available() <= 0 && (cloned || summoned || disintegrate))
		ghostPending = true;

	if(!alive())
	{
		rangedFollowUpDamagePercent = 0;
		veteranPhysicalDamageSinceActivation = 0;
		guardianSpiritHitPoints = 0;
		guardianSpiritRoundsRemaining = 0;
		// Marks belong to surviving wounds only and must never carry through death.
		regenerationRateMillionths = 0;
		regenerationPendingMicroHealth = 0;
		return;
	}
	const bool previousTopCreatureDied = health.getCount() < countBefore;
	if(previousTopCreatureDied)
	{
		// Previously marked wounds belonged to the old top creature. If it dies,
		// those marks cannot transfer to the next survivor, even after the spell
		// duration has expired.
		regenerationPendingMicroHealth = 0;
	}

	if(regenerationRateMillionths <= 0 || maximumCreatureHealth <= 0)
		return;
	static const SpellID regenerationSpell(SpellID::decode(std::string(newHorizonsMagic::NATURE_REGENERATION_SPELL)));
	const auto regenerationMarker = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(regenerationSpell))
		.And(Selector::type()(BonusType::HP_REGENERATION));
	if(!hasBonus(regenerationMarker))
		return;

	const int64_t woundsBefore = std::max<int64_t>(0,
		static_cast<int64_t>(maximumCreatureHealth) - firstHPleftBefore);
	const int64_t woundsAfter = std::max<int64_t>(0,
		static_cast<int64_t>(maximumCreatureHealth) - health.getFirstHPleft());
	// If the previous top creature died, its old wound count is irrelevant to
	// the new top survivor: all of this survivor's current missing HP came from
	// this hit. Otherwise only the increase in its existing wound is eligible.
	const int64_t newlyWoundedHealth = previousTopCreatureDied
		? woundsAfter
		: std::max<int64_t>(0, woundsAfter - woundsBefore);
	if(newlyWoundedHealth > 0)
		recordRegenerationWounds(newlyWoundedHealth);
}

int64_t CUnitState::regenerationProjectedHeal() const
{
	const int64_t survivingWounds = alive()
		? std::max<int64_t>(0, static_cast<int64_t>(getMaxHealth()) - health.getFirstHPleft())
		: 0;
	return newHorizonsMagic::regenerationHealAmount(regenerationPendingMicroHealth, survivingWounds);
}

void CUnitState::recordRegenerationWounds(const int64_t newWoundHealth)
{
	if(newWoundHealth <= 0 || regenerationRateMillionths <= 0)
		return;
	const int64_t maximum = std::numeric_limits<int64_t>::max();
	const int64_t rate = std::min<int64_t>(regenerationRateMillionths,
		newHorizonsMagic::REGENERATION_MAX_RATE_MILLIONTHS);
	const int64_t marked = newWoundHealth > maximum / rate
		? maximum
		: newWoundHealth * rate;
	regenerationPendingMicroHealth = regenerationPendingMicroHealth > maximum - marked
		? maximum
		: regenerationPendingMicroHealth + marked;
}

int64_t CUnitState::consumeRegenerationMarks()
{
	const int64_t result = regenerationProjectedHeal();
	regenerationPendingMicroHealth = 0;
	return result;
}

void CUnitState::preserveCreatureHealthOnCapacityIncrease()
{
	if(capacityHealthReferenceMax == 0)
	{
		capacityHealthReferenceMax = static_cast<int32_t>(getMaxHealth());
		capacityRegenerationRemainderTenths = 0;
	}
	health.preserveCapacityHealth();
}

void CUnitState::normalizeCapacityHealth()
{
	health.normalizeCapacityHealth(capacityHealthReferenceMax > 0);
}

int64_t CUnitState::capacityRegenerationProjectedHeal() const
{
	if(!alive() || capacityHealthReferenceMax <= 0 || !health.isCapacityHealthTracking())
		return 0;
	const int64_t numerator = static_cast<int64_t>(getMaxHealth()) + capacityRegenerationRemainderTenths;
	const auto perCreatureHeal = static_cast<int32_t>(numerator / 10);
	return health.capacityRegenerationProjectedHeal(perCreatureHeal);
}

int64_t CUnitState::consumeCapacityRegeneration()
{
	if(!alive() || capacityHealthReferenceMax <= 0 || !health.isCapacityHealthTracking())
		return 0;
	const int64_t numerator = static_cast<int64_t>(getMaxHealth()) + capacityRegenerationRemainderTenths;
	const auto perCreatureHeal = static_cast<int32_t>(numerator / 10);
	capacityRegenerationRemainderTenths = static_cast<int32_t>(numerator % 10);
	return health.consumeCapacityRegeneration(perCreatureHeal);
}

int32_t CUnitState::getCapacityHealthReferenceMax() const
{
	return capacityHealthReferenceMax > 0
		? capacityHealthReferenceMax
		: static_cast<int32_t>(getMaxHealth());
}

void CUnitState::clearCapacityHealthReference()
{
	capacityHealthReferenceMax = 0;
	capacityRegenerationRemainderTenths = 0;
	normalizeCapacityHealth();
}

HealInfo CUnitState::heal(int64_t & amount, EHealLevel level, EHealPower power)
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
		if(battleFormOriginalHealth.isBattleFormProvenance())
		{
			CHealth sourceProbe = battleFormOriginalHealth;
			CHealth effectiveProbe = health;
			int64_t sourceAllowed = amount;
			int64_t effectiveAllowed = amount;
			sourceProbe.heal(sourceAllowed, level, power);
			effectiveProbe.heal(effectiveAllowed, level, power);
			const int64_t accepted = std::min(sourceAllowed, effectiveAllowed);

			CHealth nextSource = battleFormOriginalHealth;
			CHealth nextEffective = health;
			int64_t sourceAmount = accepted;
			int64_t effectiveAmount = accepted;
			nextSource.heal(sourceAmount, level, power);
			const HealInfo result = nextEffective.heal(effectiveAmount, level, power);
			if(sourceAmount != accepted || effectiveAmount != accepted)
				throw std::logic_error("Battle-form health ledgers disagree on accepted healing");

			battleFormOriginalHealth = nextSource;
			health = nextEffective;
			amount = accepted;
			normalizeCapacityHealth();
			return result;
		}

		auto result = health.heal(amount, level, power);
		normalizeCapacityHealth();
		return result;
	}

	return {};
}

void CUnitState::afterAttack(bool ranged, bool counter, bool physical)
{
	if(counter)
		counterAttacks.use();

	if(ranged)
		shots.use();

	if(physical && waitedThisTurn)
		battlecraftWaitBonusUsed = true;
}

void CUnitState::afterWait()
{
	// Waiting is a once-per-round action.  Keep an already-spent bonus spent if
	// a malformed/repeated request reaches the state visitor during the same
	// round; the authoritative action validator remains responsible for rejecting
	// that request, but state application must not re-arm the effect.
	if(!waitedThisTurn)
		battlecraftWaitBonusUsed = false;
	waiting = true;
	waitedThisTurn = true;
}

void CUnitState::afterNewRound(bool isFirstRound)
{
	if(!isFirstRound && hasBattleForm())
	{
		// The preserved initiative only represents the remainder of the round
		// in which the form was cast. Stasis pauses form lifetime, not the round.
		battleFormInitiativeSnapshotActive = false;
		if(!isTimeStopped())
		{
			if(battleFormRoundsRemaining <= 1)
				endBattleForm();
			else
				--battleFormRoundsRemaining;
		}
	}

	if(!isFirstRound && phantomInitialIntegrity > 0 && phantomIntegrity > 0
		&& phantomRoundsRemaining > 0 && !isTimeStopped())
	{
		if(--phantomRoundsRemaining == 0)
			makeGhost();
	}

	defending = false;
	activationMovementBonus = 0;
	defensiveStanceMeleeBonus = 0;
	defensiveStanceRangedBonus = 0;
	bulwarkPreemptiveUsed = false;
	waiting = false;
	waitedThisTurn = false;
	battlecraftWaitBonusUsed = false;
	timeStopTurnConsumedFlag = false;
	movedThisRound = false;
	pursuitMovementRemaining = 0;
	cleaveUsedThisActivation = false;
	rangedFollowUpDamagePercent = 0;
	archeryCounterfireRound = -1;
	hadMorale = false;
	castSpellThisTurn = false;
	fear = false;
	drainedMana = false;
	counterAttacks.reset();

	if(alive() && isClone() && !bonusCache.hasBonus(UnitBonusValuesProxy::CLONE_MARKER))
		makeGhost();
}

void CUnitState::afterGetsTurn(BattleUnitTurnReason reason)
{
	// The corresponding STACK_GETS_TURN bonuses are removed by BattleInfo just
	// before this hook.  Clear the explicit provenance alongside them; it must
	// never survive merely because another temporary bonus used the same
	// duration.
	defending = false;
	defensiveStanceMeleeBonus = 0;
	defensiveStanceRangedBonus = 0;
	if(reason == BattleUnitTurnReason::MORALE)
	{
		hadMorale = true;
		castSpellThisTurn = false;
		movedThisRound = false;
	}
	else if(reason == BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION)
	{
		// Quartermaster creates a fresh action window, but it is not Morale and
		// does not consume or refresh per-round waiting benefits.
		castSpellThisTurn = false;
		movedThisRound = false;
		waiting = false;
	}
}

void CUnitState::makeGhost()
{
	endBattleForm();
	activationMovementBonus = 0;
	pursuitMovementRemaining = 0;
	cleaveUsedThisActivation = false;
	rangedFollowUpDamagePercent = 0;
	veteranPhysicalDamageSinceActivation = 0;
	guardianSpiritHitPoints = 0;
	guardianSpiritRoundsRemaining = 0;
	phantomIntegrity = 0;
	phantomRoundsRemaining = 0;
	capacityHealthReferenceMax = 0;
	capacityRegenerationRemainderTenths = 0;
	health.reset();
	ghostPending = true;
}

void CUnitState::onRemoved()
{
	endBattleForm();
	activationMovementBonus = 0;
	rangedFollowUpDamagePercent = 0;
	// Keep the remains ledger on a ghost until the battle result is captured.
	// Ghost stacks can be removed from the battlefield before the final result
	// is assembled; clearing the ledger here would make those direct-hit
	// casualties look like ordinary Necromancy-eligible deaths.
	health.reset(false);
	capacityHealthReferenceMax = 0;
	capacityRegenerationRemainderTenths = 0;
	guardianSpiritHitPoints = 0;
	guardianSpiritRoundsRemaining = 0;
	phantomIntegrity = 0;
	phantomRoundsRemaining = 0;
	ghostPending = false;
	ghost = true;
}

CUnitStateDetached::CUnitStateDetached(const IUnitInfo * unit_, const IBonusBearer * bonus_):
	unit(unit_),
	bonus(bonus_)
{
}

TConstBonusListPtr CUnitStateDetached::getAllBonuses(const CSelector & selector, const std::string & cachingStr) const
{
	if(!hasBattleFormState())
		return bonus->getAllBonuses(selector, cachingStr);

	const CreatureID sourceCreature = unit->unitType()->getId();
	const CreatureID effectiveCreature = battleFormCreature();
	TConstBonusListPtr originalBonuses = bonus->getAllBonuses(selector, cachingStr);
	if(effectiveCreature == sourceCreature)
		return originalBonuses;

	auto result = std::make_shared<BonusList>();
	for(const auto & bonus : *originalBonuses)
	{
		if(isBattleFormNativeBonus(bonus.get(), battleFormOriginalCreature())
			|| isBattleFormNativeBonus(bonus.get(), sourceCreature))
			continue;
		result->push_back(bonus);
	}

	const IUnitInfo * sourceUnitInfo = unit;
	while(const auto * detached = dynamic_cast<const CUnitStateDetached *>(sourceUnitInfo))
		sourceUnitInfo = detached->unit;
	const IBonusBearer * sourceBonusBearer = bonus;
	while(const auto * detached = dynamic_cast<const CUnitStateDetached *>(sourceBonusBearer))
		sourceBonusBearer = detached->bonus;
	const auto * sourceStack = dynamic_cast<const CStack *>(sourceUnitInfo);
	if(!sourceStack)
		sourceStack = dynamic_cast<const CStack *>(sourceBonusBearer);
	const auto * fallbackArmy = dynamic_cast<const CArmedInstance *>(sourceBonusBearer);
	if(!fallbackArmy)
		fallbackArmy = dynamic_cast<const CArmedInstance *>(sourceUnitInfo);
	const auto effectiveNativeBonuses = getBattleFormNativeBonuses(*this, sourceStack, fallbackArmy, selector);
	for(const auto & bonus : *effectiveNativeBonuses)
		result->push_back(bonus);
	result->stackBonuses();
	return result;
}

int32_t CUnitStateDetached::getTreeVersion() const
{
	return bonus->getTreeVersion() + getBattleFormViewRevision();
}

CUnitStateDetached & CUnitStateDetached::operator=(const CUnitState & other)
{
	CUnitState::operator=(other);
	return *this;
}

uint32_t CUnitStateDetached::unitId() const
{
	return unit->unitId();
}

BattleSide CUnitStateDetached::unitSide() const
{
	return unit->unitSide();
}

const CCreature * CUnitStateDetached::unitType() const
{
	return hasBattleFormState() ? battleFormCreature().toCreature() : unit->unitType();
}

PlayerColor CUnitStateDetached::unitOwner() const
{
	return unit->unitOwner();
}

SlotID CUnitStateDetached::unitSlot() const
{
	return unit->unitSlot();
}

int32_t CUnitStateDetached::unitBaseAmount() const
{
	return unit->unitBaseAmount();
}

void CUnitStateDetached::spendMana(ServerCallback * server, const int spellCost) const
{
	if(spellCost != 1)
		logGlobal->warn("Unexpected spell cost %d for creature", spellCost);

	//this is evil, but
	//use of netpacks in detached state is an error
	//non const API is more evil for hero
	const_cast<CUnitStateDetached *>(this)->casts.use(spellCost);
}

}
