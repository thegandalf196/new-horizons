/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../CStack.h"
#include "BattleInfo.h"
#include "PassiveDivineMandate.h"

bool CStack::unitHasAmmoCart(const battle::Unit * unit) const
{
	return battle->battleUnitHasAmmoCart(unit);
}
PlayerColor CStack::unitEffectiveOwner(const battle::Unit * unit) const
{
	return battle->battleGetOwner(unit);
}
int CStack::unitSylvanTacticsSpeed(const battle::Unit * unit, int turn) const
{
	return battle->battleSylvanTacticsSpeed(unit, turn);
}
int CStack::unitSylvanTacticsInitiativePercent(const battle::Unit * unit) const
{
	return battle->battleSylvanTacticsInitiativePercent(unit);
}
int CStack::unitFortuneSpeed(const battle::Unit * unit) const
{
	return battle->battleFortuneSpeed(unit);
}
int CStack::unitSpeedBonus(const battle::Unit * unit) const
{
	return battle->battleBloodrageSpeed(unit);
}
int CStack::unitWarfareStatBonus(const battle::Unit * unit) const
{
	return battle ? battle->battleWarfareStatBonus(unit) : 0;
}
int CStack::unitAdditionalRetaliations(const battle::Unit * unit) const
{
	return battle->battleBloodrageRetaliations(unit)
		+ newHorizonsPassiveDivineMandate::activeOrderRiders(*battle, unit).extraRetaliations;
}
int CStack::unitBloodragePainIncrement(const battle::Unit * unit) const
{
	return battle ? battle->battleBloodragePainIncrement(unit) : 0;
}
std::optional<int> CStack::unitMagicResistance(const battle::Unit * unit) const
{
	if(!battle || !unit) return std::nullopt;
	return battle->battleGetMagicResistance(unit);
}
