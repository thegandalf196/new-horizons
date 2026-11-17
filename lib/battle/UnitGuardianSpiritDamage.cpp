/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CUnitState.h"
#include "ConsecratedArms.h"
#include <stdexcept>
#include <limits>

namespace battle
{
int64_t CUnitState::getRedemptionShieldHitPoints() const
{
	return redemptionShield.remaining > 0 && alive() && mayHaveBonusType(BonusType::NONE)
		&& consecratedArms::activePrayer(*this, consecratedArms::Prayer::VICARIOUS_REDEMPTION)
		? redemptionShield.remaining : 0;
}
int64_t CUnitState::getRedemptionShieldCapacity() const
{
	return redemptionShield.initialized && redemptionShield.capacity > 0 && alive() && mayHaveBonusType(BonusType::NONE)
		&& consecratedArms::activePrayer(*this, consecratedArms::Prayer::VICARIOUS_REDEMPTION)
		? redemptionShield.capacity : 0;
}
void CUnitState::synchronizeRedemptionShield()
{
	if(!alive() || isClone() || getPhantomInitialIntegrity() > 0
		|| !mayHaveBonusType(BonusType::NONE)
		|| !consecratedArms::activePrayer(*this, consecratedArms::Prayer::VICARIOUS_REDEMPTION))
	{
		redemptionShield.clear();
		return;
	}
	redemptionShield.initialize(getBattleStartMaximumAggregateHP());
}
int64_t CUnitState::gainRedemptionShield(int64_t lostMaximumHP, int conversionBasisPoints, bool alliedHalfRate)
{
	synchronizeRedemptionShield();
	return redemptionShield.gain(lostMaximumHP, conversionBasisPoints, alliedHalfRate);
}
void CUnitState::damageWithContext(int64_t & amount, bool destroyRemains, DamageProvenance provenance,
	DamageContext context, int64_t * postShieldBeforeCasualties, bool bypassTemporaryHitPoints)
{
	if(context.bypassMagicalAbsorption && context.damageClass != DamageClass::MAGICAL)
		throw std::logic_error("Magical absorption bypass requires explicit magical damage");
	damageInternal(amount, destroyRemains, bypassTemporaryHitPoints || context.bypassMagicalAbsorption
		|| context.damageClass == DamageClass::HEALTH_COST,
		provenance, postShieldBeforeCasualties, &context);
}
void CUnitState::constrainVowDamage(int64_t & amount, const DamageContext * context, bool bypassTemporaryHitPoints) const
{
	if(!context || !context->vow || context->damageClass == DamageClass::HEALTH_COST) return;
	if(context->vow->recipient != unitId())
		throw std::logic_error("Unbroken Vow quota belongs to a different recipient");
	const auto bodyLimit = health.maximumDamageForCasualties(context->vow->remainingCasualties);
	const auto temporary = bypassTemporaryHitPoints ? 0 : health.getTemporaryHitPoints();
	if(bodyLimit < std::numeric_limits<int64_t>::max() - temporary)
		amount = std::min(amount, bodyLimit + temporary);
}
void CUnitState::absorbShields(int64_t & amount, DamageProvenance provenance, bool bypassTemporaryHitPoints,
	const DamageContext * context)
{
	if(amount <= 0 || (context && context->bypassMagicalAbsorption) || (bypassTemporaryHitPoints && !context)
		|| (context && context->damageClass == DamageClass::HEALTH_COST))
		return;
	// Preserve Guardian's exact historical route; a physical hero spell is not a creature strike.
	if(!bypassTemporaryHitPoints && provenance == DamageProvenance::PHYSICAL_CREATURE
		&& guardianSpiritHitPoints > 0 && guardianSpiritRoundsRemaining > 0)
	{
		const auto absorbed = std::min(amount, guardianSpiritHitPoints);
		amount -= absorbed;
		guardianSpiritHitPoints -= absorbed;
		if(guardianSpiritHitPoints == 0)
			guardianSpiritRoundsRemaining = 0;
	}
	synchronizeRedemptionShield();
	// Legacy damaging-spell provenance does not claim a magical damage class: both are covered.
	const bool eligible = context ? redemptionCovers(*context)
		: provenance == DamageProvenance::PHYSICAL_CREATURE || provenance == DamageProvenance::SPELL
			|| provenance == DamageProvenance::MAGICAL_CREATURE;
	if(eligible)
		redemptionShield.absorb(amount);
}
}
