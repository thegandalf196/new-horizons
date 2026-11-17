/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CUnitState.h"

namespace battle
{
void CUnitState::absorbGuardianSpiritDamage(int64_t & amount, DamageProvenance provenance, bool bypassTemporaryHitPoints)
{
	if(amount <= 0 || bypassTemporaryHitPoints)
		return;
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
