/*
 * UnitEnvironmentQueries.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include "IUnitInfo.h"
#include "IBattleInfoCallback.h"
#include "SylvanLuckState.h"

class CGHeroInstance;
class JsonNode;

namespace battle
{
class UnitOwnershipQuery;
DLL_LINKAGE bool heroHasAmmoCartArtifact(const CGHeroInstance * hero);

/// The exact input surface used by the nine callbacks; no unrelated battle API
/// can silently forward a projected query to live units or side state.
class DLL_LINKAGE UnitEnvironmentContext
{
public:
	virtual ~UnitEnvironmentContext() = default;
	virtual bool available() const = 0;
	virtual void requireSubject(const Unit & unit) const = 0;
	virtual const UnitOwnershipQuery & ownership() const = 0;
	virtual Units unitsIf(const UnitFilter & predicate) const = 0;
	virtual bool sideHasAmmoArtifact(BattleSide side) const = 0;
	virtual bool sideHasVeteranCohesion(BattleSide side) const = 0;
	virtual SylvanLuckState fortune(BattleSide side) const = 0;
	virtual int bloodrageDamage(BattleSide side) const = 0;
	virtual int bloodrageRank(BattleSide side) const = 0;
	virtual int bloodrageCap(BattleSide side) const = 0;
	virtual int bloodrageSpeed(BattleSide side) const = 0;
	virtual int bloodrageRetaliations(BattleSide side) const = 0;
	virtual int bloodragePain(BattleSide side) const = 0;
	virtual int magicResistance(const Unit * unit) const = 0;
	virtual newHorizonsNecromancy::CombatPassives necromancyPassives(BattleSide side) const = 0;
	virtual const JsonNode & magicRules() const = 0;
};

/// Shared formula owner. Context construction/metadata reads may fail during
/// preparation; this is not a commit API. Every environment callback is explicit.
class DLL_LINKAGE UnitEnvironmentQueries final : public IUnitEnvironment
{
	const UnitEnvironmentContext & context;
public:
	explicit UnitEnvironmentQueries(const UnitEnvironmentContext & context);
	bool unitHasAmmoCart(const Unit * unit) const override;
	PlayerColor unitEffectiveOwner(const Unit * unit) const override;
	std::optional<int> unitMagicResistance(const Unit * unit) const override;
	int unitSylvanTacticsSpeed(const Unit * unit, int turn = 0) const override;
	int unitSylvanTacticsInitiativePercent(const Unit * unit) const override;
	int unitFortuneSpeed(const Unit * unit) const override;
	int unitSpeedBonus(const Unit * unit) const override;
	bool unitSlowAffectsSpeed(const Unit * unit) const override;
	newHorizonsNecromancy::CombatPassives unitNecromancyPassives(const Unit * unit) const override;
	int unitAdditionalRetaliations(const Unit * unit) const override;
	int unitBloodragePainIncrement(const Unit * unit) const override;
	bool unitHasVeteranCohesion(const Unit * unit) const override;
	std::optional<std::pair<int32_t, int32_t>> unitMoraleLimits(const Unit * unit) const override;
	/// Existing non-targeted damage percentage is shared with callback consumers.
	int bloodrageDamagePercent(const Unit * unit) const;
};
}
