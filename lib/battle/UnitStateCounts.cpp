/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CUnitState.h"
namespace battle
{
int32_t CUnitState::getKilled() const
{
	const auto & provenanceHealth = battleFormOriginalHealth.isBattleFormProvenance()
		? battleFormOriginalHealth
		: health;
	int32_t res = unitBaseAmount() - provenanceHealth.getCount() + provenanceHealth.getResurrected();
	vstd::amax(res, 0);
	return res;
}

int32_t CUnitState::getMagicalCasualties() const
{
	const auto & provenanceHealth = battleFormOriginalHealth.isBattleFormProvenance()
		? battleFormOriginalHealth
		: health;
	return provenanceHealth.getCasualtyCount(DamageProvenance::SPELL);
}

bool CUnitState::hasCasualtyProvenanceState() const
{
	const auto & provenanceHealth = battleFormOriginalHealth.isBattleFormProvenance()
		? battleFormOriginalHealth
		: health;
	return provenanceHealth.hasCasualtyProvenanceState();
}

int32_t CUnitState::getCount() const
{
	return health.getCount();
}

int32_t CUnitState::getCurrentOriginalCreatureCount() const
{
	return battleFormOriginalHealth.isBattleFormProvenance()
		? battleFormOriginalHealth.getCount()
		: health.getCount();
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

}
