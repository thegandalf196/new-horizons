/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CUnitState.h"
#include "CBattleInfoCallback.h"
#include <algorithm>
#include <limits>

namespace battle
{
void CUnitState::bindStateEnvironment(const IUnitEnvironment * environment) noexcept
{
	env = environment;
	// HypotheticBattle is both a callback and an environment. Resolve that
	// dispatch once at binding, not by querying the original/live CStack.
	warfareContext = dynamic_cast<const CBattleInfoCallback *>(environment);
	shots.setEnv(env);
	counterAttacks.setEnv(env);
}

int CUnitState::warfareStatBonus() const
{
	return warfareContext ? warfareContext->battleWarfareStatBonus(this)
		: (env ? env->unitWarfareStatBonus(this) : 0);
}

int CUnitState::getAttack(bool ranged) const
{
	const int ordinary = bonusCache.getBonusValue(ranged
		? UnitBonusValuesProxy::ATTACK_RANGED : UnitBonusValuesProxy::ATTACK_MELEE);
	return static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(ordinary) + warfareStatBonus(),
		0, std::numeric_limits<int>::max()));
}

int CUnitState::getDefense(bool ranged) const
{
	const int ordinary = bonusCache.getBonusValue(ranged
		? UnitBonusValuesProxy::DEFENCE_RANGED : UnitBonusValuesProxy::DEFENCE_MELEE);
	return static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(ordinary) + warfareStatBonus(),
		0, std::numeric_limits<int>::max()));
}

int CUnitState::getDefenseIgnoringDefensiveStance(bool ranged) const
{
	const int stanceBonus = ranged ? defensiveStanceRangedBonus : defensiveStanceMeleeBonus;
	return std::max(0, getDefense(ranged) - (defended() ? stanceBonus : 0));
}
}
