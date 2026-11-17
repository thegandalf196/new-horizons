/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CUnitState.h"

namespace battle
{
bool CUnitState::canMove(int turn) const
{
	if(!alive() || isNewHorizonsFrozen())
		return false;
	if(!mayHaveBonusType(BonusType::NOT_ACTIVE))
		return true;
	if(turn == 0)
		return !hasBonusOfType(BonusType::NOT_ACTIVE);
	const std::string cachingStr = "type_NOT_ACTIVE_turns_" + std::to_string(turn);
	return !hasBonus(Selector::type()(BonusType::NOT_ACTIVE).And(Selector::turns(turn)), cachingStr);
}

bool CUnitState::isHypnotized() const
{
	return mayHaveBonusType(BonusType::HYPNOTIZED)
		&& bonusCache.hasBonus(UnitBonusValuesProxy::HYPNOTIZED);
}

bool CUnitState::isInvincible() const
{
	return mayHaveBonusType(BonusType::INVINCIBLE)
		&& bonusCache.hasBonus(UnitBonusValuesProxy::INVINCIBLE);
}
}
