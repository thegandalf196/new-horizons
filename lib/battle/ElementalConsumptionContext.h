/*
 * ElementalConsumptionContext.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include "IBattleInfoCallback.h"
#include "NewHorizonsElementalRebirth.h"

namespace newHorizonsElementalRebirth
{
/// Explicit ordered query domain for the ORIGINAL consumption validators.
/// A projected maximum-health query retains its physical identity/event program
/// in the enclosing arena; never falls back to native attach or a live roster.
class DLL_LINKAGE ElementalConsumptionContext
{
public:
	virtual ~ElementalConsumptionContext() = default;
	virtual battle::Units getUnitsIf(const battle::UnitFilter & predicate) const = 0;
	virtual const CGHeroInstance * getSideHero(BattleSide side) const = 0;
	virtual bool getPhoenixSparkUsed(BattleSide side) const = 0;
	virtual bool getRebirthChainUsed(BattleSide side) const = 0;
	virtual const newHorizonsCreatures::CreatureCategoryRules & getCreatureCategoryRules() const = 0;
	virtual std::optional<CreatureID> primaryElemental() const = 0;
	virtual int32_t maximumSummonHealth(BattleSide side, CreatureID type) = 0;
};

class DLL_LINKAGE LiveElementalConsumptionContext final : public ElementalConsumptionContext
{
	const IBattleInfo & battle;
public:
	explicit LiveElementalConsumptionContext(const IBattleInfo & battle);
	battle::Units getUnitsIf(const battle::UnitFilter & predicate) const override;
	const CGHeroInstance * getSideHero(BattleSide side) const override;
	bool getPhoenixSparkUsed(BattleSide side) const override;
	bool getRebirthChainUsed(BattleSide side) const override;
	const newHorizonsCreatures::CreatureCategoryRules & getCreatureCategoryRules() const override;
	std::optional<CreatureID> primaryElemental() const override;
	int32_t maximumSummonHealth(BattleSide side, CreatureID type) override;
};
DLL_LINKAGE void validatePhoenixSparkConsumption(ElementalConsumptionContext & battle, const PhoenixSparkConsumption & consumption);
DLL_LINKAGE void validateChainConsumption(ElementalConsumptionContext & battle, const ChainConsumption & consumption,
	const battle::UnitInfo & spawn);
DLL_LINKAGE void validateChainRollback(ElementalConsumptionContext & battle, const ChainConsumption & consumption);
DLL_LINKAGE int64_t targetHP(const DeathSnapshot & snapshot, CreatureID creature,
	const ElementalConsumptionContext & context);
}
