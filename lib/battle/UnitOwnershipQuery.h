/*
 * UnitOwnershipQuery.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include "IUnitInfo.h"

class CBattleInfoEssentials;

namespace battle
{
/// Allegiance, NOT Puppet Master action control. Matching remains asymmetric:
/// the attacker's effective owner is compared with the defender's initial side.
class DLL_LINKAGE UnitOwnershipQuery
{
public:
	virtual ~UnitOwnershipQuery() = default;
	virtual PlayerColor sidePlayer(BattleSide side) const = 0;
	BattleSide playerToSide(const PlayerColor & player) const;
	PlayerColor otherPlayer(const PlayerColor & player) const;
	PlayerColor owner(const Unit & unit) const;
	/// Action authority is distinct from allegiance; retains ordered control markers.
	PlayerColor actionController(const Unit & unit) const;
	bool matches(const Unit & attacker, const Unit & defender, bool sameOwner) const;
	bool matches(const PlayerColor & attacker, const Unit & defender, bool sameOwner) const;
};

class DLL_LINKAGE LiveUnitOwnershipQuery final : public UnitOwnershipQuery
{
	const CBattleInfoEssentials & callback;
public:
	explicit LiveUnitOwnershipQuery(const CBattleInfoEssentials & callback);
	PlayerColor sidePlayer(BattleSide side) const override;
};
}
