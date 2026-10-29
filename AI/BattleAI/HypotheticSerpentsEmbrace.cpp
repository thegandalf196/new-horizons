/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include "../../lib/battle/NewHorizonsSerpentsEmbrace.h"
void HypotheticBattle::projectSerpentBinding(uint32_t sourceId, uint32_t targetId,
	int64_t primaryDamage, bool admitted)
{
	if(!admitted || primaryDamage <= 0)
		return;
	const auto * source = battleGetUnitByID(sourceId);
	const auto * target = battleGetUnitByID(targetId);
	if(!newHorizonsSerpentsEmbrace::qualifiesAttack(*this, source, target, false, true, false))
		return;
	if(!newHorizonsSerpentsEmbrace::releasePlan(*target, sourceId).empty())
		return;
	addUnitBonus(targetId, {newHorizonsSerpentsEmbrace::marker(sourceId, source->creatureId())});
}

void HypotheticBattle::noteSerpentBindingAdmission(const Bonus & bonus) const
{
	if(newHorizonsSerpentsEmbrace::isMarker(bonus))
	{
		newHorizonsSerpentsEmbrace::sourceId(bonus);
		mayContainSerpentBindings = true;
	}
}
void HypotheticBattle::releaseSerpentBindings(uint32_t sourceId)
{
	if(mayContainSerpentBindings)
		newHorizonsSerpentsEmbrace::releaseBindings(*this, sourceId);
}

void HypotheticBattle::releaseSerpentBindingsForDeath(const battle::CUnitState & source)
{
	if(mayContainSerpentBindings)
		newHorizonsSerpentsEmbrace::releaseDeathBindings(*this, source);
}
