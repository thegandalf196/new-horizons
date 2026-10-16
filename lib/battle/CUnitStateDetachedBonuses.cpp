/*
 * CUnitStateDetachedBonuses.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "CUnitState.h"
#include "NewHorizonsCreatureAbilitySuppression.h"
#include "NewHorizonsDiscipline.h"
#include "../CStack.h"
#include "../bonuses/ProjectedBonusView.h"
#include "../mapObjects/army/CArmedInstance.h"
#include <stdexcept>

namespace battle
{
bool CUnitStateDetached::usesBonusProvider(const IBonusBearer & expected) const noexcept
{
	return bonus == &expected;
}

void CUnitStateDetached::onBattleFormChanged()
{
	CUnitState::onBattleFormChanged();
	if(const auto * projected = dynamic_cast<const ProjectedBonusView *>(bonus))
		projected->onBattleFormChanged(*this);
}

TConstBonusListPtr CUnitStateDetached::getAllBonuses(const CSelector & selector, const std::string & cachingStr) const
{
	const int32_t level = newHorizonsCreatureAbilitySuppression::suppressionLevel(*this);
	if(level == 0)
		return getBonusesBeforeCreatureAbilitySuppression(selector, cachingStr, false);
	const auto baseline = getBonusesBeforeCreatureAbilitySuppression(selector, cachingStr, true);
	return newHorizonsCreatureAbilitySuppression::filterBonuses(*this, baseline, level, true);
}

TConstBonusListPtr CUnitStateDetached::getUnstackedBonuses(const CSelector & selector) const
{
	const int32_t level = newHorizonsCreatureAbilitySuppression::suppressionLevel(*this);
	if(level == 0)
		return getBonusesBeforeCreatureAbilitySuppression(selector, {}, true);
	const auto baseline = getBonusesBeforeCreatureAbilitySuppression(selector, {}, true);
	return newHorizonsCreatureAbilitySuppression::filterBonuses(*this, baseline, level, false);
}

TConstBonusListPtr CUnitStateDetached::getBonusesBeforeCreatureAbilitySuppression(const CSelector & selector,
	const std::string & cachingStr, const bool unstacked) const
{
	if(const auto * projected = dynamic_cast<const ProjectedBonusView *>(bonus))
	{
		if(projected->evaluatedUnit() != this)
			throw std::logic_error("Projected bonus view belongs to another detached unit");
		return newHorizonsDiscipline::veteranCohesionBonuses(unstacked
			? bonus->getUnstackedBonuses(selector) : bonus->getAllBonuses(selector, cachingStr), selector, veteranCohesionEarned);
	}
	const auto sourceBonuses = [this, &selector, &cachingStr, unstacked]()
	{
		if(const auto * sourceUnit = dynamic_cast<const Unit *>(bonus))
			return newHorizonsDiscipline::veteranCohesionBonuses(
				sourceUnit->getBonusesBeforeCreatureAbilitySuppression(selector, cachingStr, unstacked), selector, veteranCohesionEarned);

		return newHorizonsDiscipline::veteranCohesionBonuses(unstacked
			? bonus->getUnstackedBonuses(selector) : bonus->getAllBonuses(selector, cachingStr), selector, veteranCohesionEarned);
	};

	if(!hasBattleFormState())
		return sourceBonuses();

	const CreatureID sourceCreature = unit->unitType()->getId();
	const CreatureID effectiveCreature = battleFormCreature();
	const auto originalBonuses = sourceBonuses();
	if(effectiveCreature == sourceCreature)
		return originalBonuses;

	auto result = std::make_shared<BonusList>();
	for(const auto & bonus : *originalBonuses)
	{
		if(isBattleFormNativeBonus(bonus.get(), battleFormOriginalCreature())
			|| isBattleFormNativeBonus(bonus.get(), sourceCreature))
			continue;
		result->push_back(bonus);
	}

	const IUnitInfo * sourceUnitInfo = unit;
	while(const auto * detached = dynamic_cast<const CUnitStateDetached *>(sourceUnitInfo))
		sourceUnitInfo = detached->unit;
	const IBonusBearer * sourceBonusBearer = bonus;
	while(const auto * detached = dynamic_cast<const CUnitStateDetached *>(sourceBonusBearer))
		sourceBonusBearer = detached->bonus;
	const auto * sourceStack = dynamic_cast<const CStack *>(sourceUnitInfo);
	if(!sourceStack)
		sourceStack = dynamic_cast<const CStack *>(sourceBonusBearer);
	const auto * fallbackArmy = dynamic_cast<const CArmedInstance *>(sourceBonusBearer);
	if(!fallbackArmy)
		fallbackArmy = dynamic_cast<const CArmedInstance *>(sourceUnitInfo);
	const auto effectiveNativeBonuses = getBattleFormNativeBonuses(*this, sourceStack, fallbackArmy, selector, unstacked);
	for(const auto & nativeBonus : *effectiveNativeBonuses)
		result->push_back(nativeBonus);
	if(!unstacked)
		result->stackBonuses();
	return result;
}

int32_t CUnitStateDetached::getTreeVersion() const
{
	return bonus->getTreeVersion() + getBattleFormViewRevision() + (veteranCohesionEarned ? 1 : 0);
}

bool CUnitStateDetached::isBonusCacheable() const
{
	return bonus->isBonusCacheable();
}
}
