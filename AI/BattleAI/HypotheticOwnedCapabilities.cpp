/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/OwnedBattleFrameBatch.h"
#include "../../lib/battle/PossiblePlayerBattleAction.h"
#include <stdexcept>
int32_t HypotheticBattle::getActivationSerial() const
{
	return usesOwnedFrameQueries() ? ownedFrameBatch->activationSerial() : BattleProxy::getActivationSerial();
}
DamageEstimation HypotheticBattle::calculateDmgRange(const BattleAttackInfo & info) const
{
	if(usesOwnedFrameQueries()) return ownedFrameBatch->querySubject()->calculateDmgRange(info);
	return CBattleInfoCallback::calculateDmgRange(info);
}
bool HypotheticBattle::battleCanTargetEmptyHex(const battle::Unit * attacker) const
{
	if(usesOwnedFrameQueries()) return ownedFrameBatch->querySubject()->battleCanTargetEmptyHex(attacker);
	return CBattleInfoCallback::battleCanTargetEmptyHex(attacker);
}
void HypotheticBattle::configurePassingLines(const battle::Unit * mover, ReachabilityInfo::Parameters & params) const
{
	if(usesOwnedFrameQueries()) { ownedFrameBatch->querySubject()->configurePassingLines(mover, params); return; }
	CBattleInfoCallback::configurePassingLines(mover, params);
}
std::vector<SpellID> HypotheticBattle::getAvailableBeneficialSpells(const battle::Unit * caster, const battle::Unit * target) const
{
	if(usesOwnedFrameQueries()) return ownedFrameBatch->querySubject()->getAvailableBeneficialSpells(caster, target);
	return CBattleInfoCallback::getAvailableBeneficialSpells(caster, target);
}
std::vector<SpellID> HypotheticBattle::getAvailableRandomCreatureSpells(const battle::Unit * caster, const battle::Unit * target) const
{
	if(usesOwnedFrameQueries()) return ownedFrameBatch->querySubject()->getAvailableRandomCreatureSpells(caster, target);
	return CBattleInfoCallback::getAvailableRandomCreatureSpells(caster, target);
}
int HypotheticBattle::battleGetSurrenderCost(const PlayerColor & player) const
{
	if(usesOwnedFrameQueries()) return ownedFrameBatch->querySubject()->battleGetSurrenderCost(player);
	return CBattleInfoCallback::battleGetSurrenderCost(player);
}
std::vector<PossiblePlayerBattleAction> HypotheticBattle::getClientActionsForStack(const CStack * stack, const BattleClientInterfaceData & data)
{
	if(usesOwnedFrameQueries()) return ownedFrameBatch->querySubject()->getClientActionsForStack(stack, data);
	return CBattleInfoCallback::getClientActionsForStack(stack, data);
}
int HypotheticBattle::battleGetRetreatPermission(BattleSide side, BonusType permission) const
{
	if(usesOwnedFrameQueries()) throw std::logic_error("Owned retreat permission requires its captured canonical context");
	return CBattleInfoEssentials::battleGetRetreatPermission(side, permission);
}
const std::map<CreatureID, TQuantity> & HypotheticBattle::getDemonicReserve(BattleSide side) const
{
	if(usesOwnedFrameQueries()) throw std::logic_error("Owned reserve metadata was not captured");
	return IBattleInfo::getDemonicReserve(side);
}
