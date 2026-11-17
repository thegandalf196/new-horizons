/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "UnitShieldState.h"
#include "Unit.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace battle
{
std::optional<ShieldDamagePreview> previewShieldDamage(const Unit & unit, int64_t incoming,
	DamageProvenance provenance, const DamageContext * context, bool bypassTemporaryHP)
{
	incoming = std::max<int64_t>(0, incoming);
	const bool healthCost = context && context->damageClass == DamageClass::HEALTH_COST;
	if(healthCost) bypassTemporaryHP = true;
	const bool bypassAbsorption = context && context->bypassMagicalAbsorption;
	if(bypassAbsorption && context->damageClass != DamageClass::MAGICAL)
		throw std::logic_error("Magical absorption bypass requires explicit magical damage");
	if(bypassAbsorption) bypassTemporaryHP = true;

	if(!healthCost && !bypassAbsorption && (!bypassTemporaryHP || context))
	{
		if(!bypassTemporaryHP && provenance == DamageProvenance::PHYSICAL_CREATURE
			&& unit.getGuardianSpiritRoundsRemaining() > 0)
			incoming -= std::min(incoming, unit.getGuardianSpiritHitPoints());
		const bool covered = context ? redemptionCovers(*context)
			: provenance == DamageProvenance::PHYSICAL_CREATURE || provenance == DamageProvenance::SPELL
				|| provenance == DamageProvenance::MAGICAL_CREATURE;
		if(covered) incoming -= std::min(incoming, unit.getRedemptionShieldHitPoints());
	}
	ShieldDamagePreview result;
	result.postShield = incoming;
	if(incoming == 0) return result;
	if(unit.isClone())
	{
		result.healthLoss = unit.getAvailableHealth();
		return result;
	}
	const auto temporary = bypassTemporaryHP ? 0
		: std::max<int64_t>(0, unit.getAvailableHealth() - unit.getShadowGiftCurrentHealth());
	if(context && context->vow && !healthCost && unit.getPhantomInitialIntegrity() == 0)
	{
		if(context->vow->recipient != unit.unitId()) return std::nullopt;
		const auto limit = unit.maximumBodyDamageForCasualties(context->vow->remainingCasualties);
		if(!limit) return std::nullopt;
		if(*limit < std::numeric_limits<int64_t>::max() - temporary)
			incoming = std::min(incoming, *limit + temporary);
	}
	const auto available = bypassTemporaryHP ? unit.getShadowGiftCurrentHealth() : unit.getAvailableHealth();
	result.appliedDamage = std::min(incoming, available);
	result.healthLoss = result.appliedDamage;
	result.bodyHealthLoss = result.appliedDamage - std::min(temporary, result.appliedDamage);
	return result;
}
}
