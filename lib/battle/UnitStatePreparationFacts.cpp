/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CUnitState.h"
#include <stdexcept>
namespace
{
std::optional<battle::UnitPreparationHealthBasis> preparationBasis(const battle::IUnitInfo & info)
{
	const auto facts = info.unitPreparationHealthBasis();
	if(facts && (facts->battleStartMaximumHP < 0 || facts->rebirthOriginalHP < 0))
		throw std::invalid_argument("Negative copied unit preparation health basis");
	return facts;
}
}
namespace battle
{
std::optional<UnitPreparationHealthBasis> CUnitStateDetached::unitPreparationHealthBasis() const
{
	return unit->unitPreparationHealthBasis();
}

int64_t CUnitStateDetached::getBattleStartMaximumAggregateHP() const
{
	if(const auto basis = preparationBasis(*unit)) return basis->battleStartMaximumHP;
	if(const auto * detachedSource = dynamic_cast<const CUnitStateDetached *>(unit))
	{
		const auto basis = detachedSource->getBattleStartMaximumAggregateHP();
		if(basis > 0)
			return basis;
	}
	else if(const auto * sourceUnit = dynamic_cast<const Unit *>(unit))
	{
		const auto basis = sourceUnit->getBattleStartMaximumAggregateHP();
		if(basis > 0)
			return basis;
	}
	if(const auto * detachedBonus = dynamic_cast<const CUnitStateDetached *>(bonus))
		return detachedBonus->getBattleStartMaximumAggregateHP();
	const auto * sourceBonus = dynamic_cast<const Unit *>(bonus);
	return sourceBonus ? sourceBonus->getBattleStartMaximumAggregateHP() : 0;
}

int64_t CUnitStateDetached::getRebirthOriginalAggregateHP() const
{
	if(const auto basis = preparationBasis(*unit)) return basis->rebirthOriginalHP;
	if(const auto * detachedSource = dynamic_cast<const CUnitStateDetached *>(unit))
	{
		const auto originalHP = detachedSource->getRebirthOriginalAggregateHP();
		if(originalHP > 0)
			return originalHP;
	}
	else if(const auto * sourceUnit = dynamic_cast<const Unit *>(unit))
	{
		const auto originalHP = sourceUnit->getRebirthOriginalAggregateHP();
		if(originalHP > 0)
			return originalHP;
	}
	if(const auto * detachedBonus = dynamic_cast<const CUnitStateDetached *>(bonus))
		return detachedBonus->getRebirthOriginalAggregateHP();
	const auto * sourceBonus = dynamic_cast<const Unit *>(bonus);
	return sourceBonus ? sourceBonus->getRebirthOriginalAggregateHP() : 0;
}

}
