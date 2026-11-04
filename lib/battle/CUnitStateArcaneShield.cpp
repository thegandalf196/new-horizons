/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CUnitState.h"

namespace battle
{
void CUnitState::absorbDamageShields(int64_t & amount, DamageProvenance provenance, bool bypassShields)
{
	if(amount <= 0)
		return;
	if(arcaneShieldHitPoints < 0)
		throw std::logic_error("Invalid Arcane Shield absorption pool");
	if(!bypassShields)
	{
		const auto absorbed = std::min(amount, arcaneShieldHitPoints);
		amount -= absorbed;
		arcaneShieldHitPoints -= absorbed;
	}
	if(amount > 0 && provenance == DamageProvenance::PHYSICAL_CREATURE
		&& guardianSpiritHitPoints > 0 && guardianSpiritRoundsRemaining > 0)
	{
		const auto absorbed = std::min(amount, guardianSpiritHitPoints);
		amount -= absorbed;
		guardianSpiritHitPoints -= absorbed;
		if(guardianSpiritHitPoints == 0)
			guardianSpiritRoundsRemaining = 0;
	}
}
}
