/* Part of VCMI / New Horizons. GPL-2.0-or-later; authors listed in AUTHORS. */
#include "StdInc.h"
#include "PhysicalPoisonValue.h"
#include "AttackPossibility.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/battle/CUnitState.h"
int64_t physicalPoisonValue::projectedDamage(const battle::CUnitState * state)
{
	if(!state || state->physicalPoisonBaseDamage <= 0 || state->physicalPoisonActivationsRemaining <= 0)
		return 0;
	const int64_t base = state->physicalPoisonBaseDamage;
	const int remaining = state->physicalPoisonActivationsRemaining;
	if(remaining >= 3)
		return base * 4 + base / 2;
	if(remaining == 2)
		return base + base / 2 + base * 2;
	return base * 2;
}

void physicalPoisonValue::applySpines(AttackPossibility & preview, battle::CUnitState * reflectedFrom,
	int64_t actualReflectedDamage, bool ranged, int32_t currentRound,
	const std::shared_ptr<CBattleInfoCallback> & battle, DamageCache & damageCache)
{
	const auto * bulwarkHero = battle->battleGetOwnerHero(reflectedFrom);
	if(!ranged && newHorizonsBulwark::hasToxicSpines(bulwarkHero)
		&& reflectedFrom->bulwarkToxicSpinesRound != currentRound
		&& actualReflectedDamage > 0 && preview.attackerState->alive())
	{
		reflectedFrom->bulwarkToxicSpinesRound = currentRound;
		const auto previousPoisonDamage = projectedDamage(preview.attackerState.get());
		const auto poisonBase = newHorizonsBulwark::toxicSpinesPoisonBase(actualReflectedDamage);
		if(newHorizonsBulwark::applyPhysicalPoison(preview.attackerState.get(), poisonBase,
			static_cast<int32_t>(reflectedFrom->unitId())))
		{
			const auto projectedPoisonDamage = projectedDamage(preview.attackerState.get());
			const auto residualPoisonDamage = std::max<int64_t>(0,
				projectedPoisonDamage - previousPoisonDamage);
			if(residualPoisonDamage > 0)
				preview.attackerDamageReduce += AttackPossibility::calculateDamageReduce(reflectedFrom,
					preview.attackerState.get(), residualPoisonDamage, damageCache, battle);
		}
	}
}
