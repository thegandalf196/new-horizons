/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsElementalAttunement.h"
#include "CUnitState.h"
#include "BattleInfo.h"
#include "LiveUnitEnvironmentContext.h"
#include "../CStack.h"
#include "../bonuses/BonusList.h"

newHorizonsElementalAttunement::UnitPassives battle::CUnitState::elementalAttunementPassives() const
{
	return env ? env->unitElementalAttunement(this) : newHorizonsElementalAttunement::UnitPassives{};
}

newHorizonsElementalAttunement::UnitPassives CStack::unitElementalAttunement(const battle::Unit * unit) const
{
	if(!battle || !unit) return {};
	const battle::LiveUnitEnvironmentContext context(*battle);
	return battle::UnitEnvironmentQueries(context).unitElementalAttunement(unit);
}

uint32_t newHorizonsElementalAttunement::maximumHealth(const battle::Unit & unit, int ordinary)
{
	const int percent = unit.elementalAttunementPassives().maximumHealthPercent;
	if(percent == 0) return std::max(1, ordinary);
	// Reuse ordinary STACK_HEALTH value modes, rather than multiplying the
	// already rounded result or modifying any wounds, casualties or counts.
	BonusList health(*unit.getAllBonuses(Selector::type()(BonusType::STACK_HEALTH)));
	static const auto bonus = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::STACK_HEALTH, BonusSource::SECONDARY_SKILL, 25,
		BonusSourceID(), BonusSubtypeID(), BonusValueType::PERCENT_TO_BASE);
	health.push_back(bonus);
	return std::max(1, health.totalValue());
}

uint32_t battle::CUnitState::getMaxHealth() const
{
	const int ordinary = std::max(1, bonusCache.getBonusValue(UnitBonusValuesProxy::STACK_HEALTH));
	return newHorizonsElementalAttunement::maximumHealth(*this, ordinary);
}

