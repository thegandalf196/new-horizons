/*
 * LiveUnitEnvironmentContext.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include "UnitEnvironmentQueries.h"
#include "UnitOwnershipQuery.h"

class CBattleInfoCallback;
namespace battle
{
/// Lazy live metadata adapter: each formula still requests only its old inputs
/// at the old query point. No eager capture of unrelated hero bonuses.
class DLL_LINKAGE LiveUnitEnvironmentContext final : public UnitEnvironmentContext
{
	const CBattleInfoCallback & callback;
	LiveUnitOwnershipQuery owners;
public:
	explicit LiveUnitEnvironmentContext(const CBattleInfoCallback & callback);
	bool available() const override;
	void requireSubject(const Unit &) const override {}
	const UnitOwnershipQuery & ownership() const override;
	Units unitsIf(const UnitFilter & predicate) const override;
	bool sideHasAmmoArtifact(BattleSide side) const override;
	bool sideHasVeteranCohesion(BattleSide side) const override;
	SylvanLuckState fortune(BattleSide side) const override;
	int bloodrageDamage(BattleSide side) const override;
	int bloodrageRank(BattleSide side) const override;
	int bloodrageCap(BattleSide side) const override;
	int bloodrageSpeed(BattleSide side) const override;
	int bloodrageRetaliations(BattleSide side) const override;
	int bloodragePain(BattleSide side) const override;
	int magicResistance(const Unit * unit) const override;
	const JsonNode & magicRules() const override;
};
}
