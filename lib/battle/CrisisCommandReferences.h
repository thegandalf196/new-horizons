/*
 * CrisisCommandReferences.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include "IBattleInfoCallback.h"
#include "NewHorizonsCrisisCommand.h"
#include "Unit.h"

namespace newHorizonsCrisisCommand
{
/// Exact read-only reference domain used by the existing Crisis validators.
/// Implementations retain complete ordered units and immutable hero metadata;
/// this interface does not grant mutation, authority or a world ownership lease.
class DLL_LINKAGE CrisisCommandReferences
{
public:
	virtual ~CrisisCommandReferences() = default;
	virtual battle::Units getUnitsIf(const battle::UnitFilter & predicate) const = 0;
	virtual const CGHeroInstance * getSideHero(BattleSide side) const = 0;
	virtual const SeizeInitiativeState & getSeizeInitiativeState() const = 0;
	virtual int32_t getRound() const = 0;
	virtual const HeroActionAllowanceState & getHeroActionAllowances(BattleSide side) const = 0;
};

class DLL_LINKAGE LiveCrisisReferences final : public CrisisCommandReferences
{
	const IBattleInfo & battle;
public:
	explicit LiveCrisisReferences(const IBattleInfo & battle);
	battle::Units getUnitsIf(const battle::UnitFilter & predicate) const override;
	const CGHeroInstance * getSideHero(BattleSide side) const override;
	const SeizeInitiativeState & getSeizeInitiativeState() const override;
	int32_t getRound() const override;
	const HeroActionAllowanceState & getHeroActionAllowances(BattleSide side) const override;
};

DLL_LINKAGE void validateCrisisSerializedReferences(const State & state, const CrisisCommandReferences & battle,
	BattleSide replacementSide = BattleSide::NONE, const HeroActionAllowanceState * replacementLedger = nullptr);
DLL_LINKAGE void validateCrisisState(const State & state, const CrisisCommandReferences & battle,
	BattleSide replacementSide = BattleSide::NONE, const HeroActionAllowanceState * replacementLedger = nullptr);
}
