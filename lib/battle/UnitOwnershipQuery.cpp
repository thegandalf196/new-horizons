/*
 * UnitOwnershipQuery.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "UnitOwnershipQuery.h"
#include "CBattleInfoEssentials.h"
#include "IBattleState.h"
#include "Unit.h"
#include "NewHorizonsPuppetMaster.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"

battle::LiveUnitOwnershipQuery::LiveUnitOwnershipQuery(const CBattleInfoEssentials & callback) : callback(callback) {}
PlayerColor battle::LiveUnitOwnershipQuery::sidePlayer(BattleSide side) const
{
	return callback.getBattle()->getSidePlayer(side);
}

BattleSide battle::UnitOwnershipQuery::playerToSide(const PlayerColor & player) const
{
	if(sidePlayer(BattleSide::ATTACKER) == player)
		return BattleSide::ATTACKER;
	if(sidePlayer(BattleSide::DEFENDER) == player)
		return BattleSide::DEFENDER;
	logGlobal->warn("Cannot find side for player %s", player.toString());
	return BattleSide::INVALID;
}

PlayerColor battle::UnitOwnershipQuery::otherPlayer(const PlayerColor & player) const
{
	auto side = playerToSide(player);
	if(side == BattleSide::NONE)
		return PlayerColor::CANNOT_DETERMINE;
	return sidePlayer(CBattleInfoEssentials::otherSide(side));
}

PlayerColor battle::UnitOwnershipQuery::owner(const Unit & unit) const
{
	PlayerColor initialOwner = sidePlayer(unit.unitSide());
	if(unit.isHypnotized())
		return otherPlayer(initialOwner);
	else
		return initialOwner;
}

bool battle::UnitOwnershipQuery::matches(const Unit & attacker, const Unit & defender, bool sameOwner) const
{
	if(attacker.unitId() == defender.unitId())
		return sameOwner;
	else
		return matches(owner(attacker), defender, sameOwner);
}

bool battle::UnitOwnershipQuery::matches(const PlayerColor & attacker, const Unit & defender, bool sameOwner) const
{
	PlayerColor initialOwner = sidePlayer(defender.unitSide());
	return (attacker == initialOwner) == sameOwner;
}

BattleSide CBattleInfoEssentials::playerToSide(const PlayerColor & player) const
{
	RETURN_IF_NOT_BATTLE(BattleSide::NONE);
	return battle::LiveUnitOwnershipQuery(*this).playerToSide(player);
}

PlayerColor CBattleInfoEssentials::otherPlayer(const PlayerColor & player) const
{
	RETURN_IF_NOT_BATTLE(PlayerColor::CANNOT_DETERMINE);
	return battle::LiveUnitOwnershipQuery(*this).otherPlayer(player);
}

PlayerColor CBattleInfoEssentials::battleGetOwner(const battle::Unit * unit) const
{
	RETURN_IF_NOT_BATTLE(PlayerColor::CANNOT_DETERMINE);
	return battle::LiveUnitOwnershipQuery(*this).owner(*unit);
}

bool CBattleInfoEssentials::battleMatchOwner(const battle::Unit * attacker, const battle::Unit * defender, const bool sameOwner) const
{
	RETURN_IF_NOT_BATTLE(false);
	return battle::LiveUnitOwnershipQuery(*this).matches(*attacker, *defender, sameOwner);
}

bool CBattleInfoEssentials::battleMatchOwner(const PlayerColor & attacker, const battle::Unit * defender, const bool sameOwner) const
{
	RETURN_IF_NOT_BATTLE(false);
	return battle::LiveUnitOwnershipQuery(*this).matches(attacker, *defender, sameOwner);
}

PlayerColor battle::UnitOwnershipQuery::actionController(const Unit & unit) const
{
	const auto allegianceOwner = owner(unit);

	const auto markers = unit.getBonuses(Selector::type()(BonusType::PUPPET_MASTER_CONTROL));
	if(!markers)
		return allegianceOwner;

	for(const auto & marker : *markers)
		if(marker && newHorizonsPuppetMaster::isValidControlMarker(*this, &unit, marker.get()))
			return marker->spellCasterOwner;

	// Invalid or legacy control metadata must never grant authority to an
	// arbitrary player. Fall back to the existing allegiance controller.
	return allegianceOwner;
}
