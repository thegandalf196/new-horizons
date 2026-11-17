/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "UnitShieldState.h"
#include "Unit.h"
#include "../bonuses/BonusList.h"
#include "../serializer/JsonSerializeFormat.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace battle
{
VowCasualtyQuota VowCasualtyQuota::capture(uint32_t recipient, int32_t count, int casualtyCeilingBasisPoints)
{
	if(count < 0 || casualtyCeilingBasisPoints < 0 || casualtyCeilingBasisPoints > 10000)
		throw std::invalid_argument("Invalid Unbroken Vow casualty quota");
	const auto ceiling = (static_cast<int64_t>(count) * casualtyCeilingBasisPoints + 9999) / 10000;
	return {recipient, count, static_cast<int32_t>(ceiling)};
}
int64_t VowCasualtyQuota::maximumBodyDamage(int32_t count, int32_t firstHP, int64_t maximumCreatureHP) const
{
	if(count < 0 || firstHP < 0 || maximumCreatureHP <= 0 || firstHP > maximumCreatureHP
		|| initialCount < 0 || remainingCasualties < 0 || remainingCasualties > initialCount)
		throw std::invalid_argument("Invalid Unbroken Vow recipient health");
	if(count == 0) return 0;
	if(remainingCasualties >= count) return std::numeric_limits<int64_t>::max();
	return firstHP + static_cast<int64_t>(remainingCasualties) * maximumCreatureHP - 1;
}
void VowCasualtyQuota::consume(int32_t before, int32_t after)
{
	if(before < after || after < 0 || before - after > remainingCasualties)
		throw std::logic_error("Unbroken Vow exceeded its logical-event casualty quota");
	remainingCasualties -= before - after;
}
void RedemptionShieldState::clear() noexcept
{
	*this = {};
}
void RedemptionShieldState::initialize(int64_t combatStartMaximumHP)
{
	if(combatStartMaximumHP < 0)
		throw std::invalid_argument("Negative Redemption combat-start HP");
	if(!initialized)
	{
		capacity = combatStartMaximumHP / 5;
		initialized = true;
	}
	validate();
}
int64_t RedemptionShieldState::gain(int64_t lostMaximumHP, int conversionBasisPoints, bool alliedHalfRate)
{
	validate();
	if(lostMaximumHP < 0 || conversionBasisPoints < 0 || conversionBasisPoints > 5000)
		throw std::invalid_argument("Invalid Redemption casualty conversion");
	if(!initialized || remaining == capacity)
		return 0;
	const int64_t factor = conversionBasisPoints * (alliedHalfRate ? 1 : 2);
	// Divide before multiplying: both full and half conversion remain exact and bounded.
	const int64_t whole = (lostMaximumHP / 20000) * factor;
	const int64_t fraction = (lostMaximumHP % 20000) * factor + fractionalHP;
	const int64_t requested = whole + fraction / 20000;
	const int64_t accepted = std::min(requested, capacity - remaining);
	remaining += accepted;
	fractionalHP = remaining == capacity ? 0 : static_cast<int32_t>(fraction % 20000);
	return accepted;
}
int64_t RedemptionShieldState::absorb(int64_t & damage)
{
	validate();
	const auto absorbed = std::min(std::max<int64_t>(0, damage), remaining);
	remaining -= absorbed;
	damage -= absorbed;
	return absorbed;
}
bool RedemptionShieldState::empty() const noexcept
{
	return !initialized && remaining == 0 && capacity == 0 && fractionalHP == 0;
}
void RedemptionShieldState::validate() const
{
	if(remaining < 0 || capacity < 0 || remaining > capacity
		|| fractionalHP < 0 || fractionalHP >= 20000
		|| (!initialized && (remaining || capacity || fractionalHP))
		|| (remaining == capacity && fractionalHP != 0))
		throw std::runtime_error("Invalid Redemption shield state");
}
void RedemptionShieldState::serializeJson(JsonSerializeFormat & handler)
{
	handler.serializeInt("redemptionShieldHP", remaining, 0);
	handler.serializeInt("redemptionShieldCapacity", capacity, 0);
	handler.serializeInt("redemptionShieldFractionalHP", fractionalHP, 0);
	handler.serializeBool("redemptionShieldInitialized", initialized);
	validate();
}
bool redemptionCovers(DamageContext damage)
{
	return damage.damageClass == DamageClass::PHYSICAL || damage.damageClass == DamageClass::MAGICAL;
}
ShieldSnapshot shieldSnapshot(const Unit & unit)
{
	int64_t guardianCapacity = 0;
	const auto markers = unit.getGuardianSpiritHitPoints() > 0
		? unit.getBonuses(Selector::type()(BonusType::GUARDIAN_SPIRIT)) : nullptr;
	if(markers)
		for(const auto & marker : *markers)
			if(marker && marker->turnsRemain > 0) guardianCapacity = std::max<int64_t>(guardianCapacity, marker->val);
	ShieldSnapshot result;
	const auto guardianRemaining = unit.alive() && guardianCapacity > 0 && unit.getGuardianSpiritRoundsRemaining() > 0
		? unit.getGuardianSpiritHitPoints() : 0;
	result.pools[0] = {ShieldSource::GUARDIAN_SPIRIT, guardianRemaining,
		guardianRemaining > 0 ? std::max(guardianCapacity, guardianRemaining) : 0, true};
	result.pools[1] = {ShieldSource::VICARIOUS_REDEMPTION, unit.getRedemptionShieldHitPoints(),
		unit.getRedemptionShieldCapacity(), false};
	return result;
}
}
