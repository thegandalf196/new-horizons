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
#include <set>

namespace battle
{
CUnitStateDetached::CUnitStateDetached(const IUnitInfo * unit_, const IBonusBearer * bonus_):
	unit(unit_),
	bonus(bonus_)
{
}

void CUnitStateDetached::captureBonusProviderDispatch()
{
	// Called by acquire/acquireState before the wrapper is exposed. No bonus
	// values or evaluatedUnit binding are captured, and queries never mutate it.
	bonusDispatchProjected = dynamic_cast<const ProjectedBonusView *>(bonus);
	bonusDispatchUnit = bonusDispatchProjected ? nullptr : dynamic_cast<const Unit *>(bonus);
	bonusDispatchType = bonus ? &typeid(*bonus) : nullptr;
}

bool CUnitStateDetached::hasCurrentBonusProviderDispatch() const
{
	return bonusDispatchType && *bonusDispatchType == typeid(*bonus);
}

const ProjectedBonusView * CUnitStateDetached::projectedBonusProvider() const
{
	return hasCurrentBonusProviderDispatch() ? bonusDispatchProjected
		: dynamic_cast<const ProjectedBonusView *>(bonus);
}

const Unit * CUnitStateDetached::unitBonusProvider() const
{
	return hasCurrentBonusProviderDispatch() ? bonusDispatchUnit
		: dynamic_cast<const Unit *>(bonus);
}

bool CUnitStateDetached::usesBonusProvider(const IBonusBearer & expected) const noexcept
{
	return bonus == &expected;
}

const IBonusBearer * CUnitStateDetached::getBonusBearer() const
{
	if(const auto * projected = projectedBonusProvider())
	{
		if(projected->evaluatedUnit() != this)
			throw std::logic_error("Projected bonus view belongs to another detached unit");
		return projected;
	}
	return this; // Ordinary detached wrappers retain their existing query identity.
}

const IBonusBearer * CUnitStateDetached::sourceLocalEffectProvider() const
{
	std::set<const CUnitStateDetached *> visited;
	const CUnitStateDetached * context = this;
	while(true)
	{
		if(!visited.insert(context).second)
			throw std::logic_error("Cyclic detached local-effect provenance");
		if(const auto * projected = context->projectedBonusProvider())
		{
			if(projected->evaluatedUnit() != context)
				throw std::logic_error("Local-effect provenance belongs to another detached unit");
			return projected;
		}
		const auto * source = context->unitBonusProvider();
		if(!source || !context->hasUnitInfoAncestor(*source) || !context->hasBonusProviderAncestor(*source))
			throw std::runtime_error("Detached local-effect provenance requires its actual info and provider");
		if(const auto * detached = dynamic_cast<const CUnitStateDetached *>(source))
			context = detached;
		else
			return source;
	}
}

void CUnitStateDetached::onBattleFormChanged()
{
	CUnitState::onBattleFormChanged();
	if(const auto * projected = projectedBonusProvider())
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
	if(const auto * projected = projectedBonusProvider())
	{
		if(projected->evaluatedUnit() != this)
			throw std::logic_error("Projected bonus view belongs to another detached unit");
		return newHorizonsDiscipline::veteranCohesionBonuses(unstacked
			? bonus->getUnstackedBonuses(selector) : bonus->getAllBonuses(selector, cachingStr), selector, veteranCohesionEarned);
	}
	const auto sourceBonuses = [this, &selector, &cachingStr, unstacked]()
	{
		if(const auto * sourceUnit = unitBonusProvider())
			return newHorizonsDiscipline::veteranCohesionBonuses(
				sourceUnit->getBonusesBeforeCreatureAbilitySuppression(selector, cachingStr, unstacked), selector, veteranCohesionEarned);

		return newHorizonsDiscipline::veteranCohesionBonuses(unstacked
			? bonus->getUnstackedBonuses(selector) : bonus->getAllBonuses(selector, cachingStr), selector, veteranCohesionEarned);
	};

	const bool formState = hasBattleFormState();
	if(!formState && !unitCreatureResolver())
		return sourceBonuses(); // Legacy provider keeps the original no-form return.

	const auto * ownedSource = completeOwnedFormSource();
	if(!formState && !ownedSource)
		return sourceBonuses();
	const CreatureID sourceCreature = ownedSource ? ownedSource->unitType()->getId() : unit->unitType()->getId();
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
	auto effectiveNativeBonuses = ownedNativeFormBonuses(*this, selector, unstacked);
	if(!effectiveNativeBonuses)
		effectiveNativeBonuses = getBattleFormNativeBonuses(*this, sourceStack, fallbackArmy, selector, unstacked);
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

bool battle::CUnitStateDetached::usesUnitInfo(const IUnitInfo & expected) const noexcept
{
	return unit == &expected;
}

bool battle::CUnitStateDetached::hasUnitInfoAncestor(const IUnitInfo & expected) const
{
	std::set<const IUnitInfo *> visited;
	const IUnitInfo * value = unit;
	while(value)
	{
		if(value == &expected) return true;
		if(!visited.insert(value).second) throw std::logic_error("Cyclic detached unit-info ancestry");
		const auto * detached = dynamic_cast<const CUnitStateDetached *>(value);
		if(!detached) return false;
		value = detached->unit;
	}
	return false;
}
TConstBonusListPtr battle::CUnitStateDetached::ownedNativeFormBonuses(
	const CUnitState & form, const CSelector & selector, bool unstacked) const
{
	if(const auto * view = projectedBonusProvider())
	{
		if(view->evaluatedUnit() != this) throw std::logic_error("Owned form source view belongs to another unit");
		return view->ownedNativeFormBonuses(form, selector, unstacked);
	}
	if(const auto * source = unitBonusProvider()) return source->ownedNativeFormBonuses(form, selector, unstacked);
	return {};
}

bool battle::CUnitStateDetached::hasBonusProviderAncestor(const IBonusBearer & expected) const
{
	std::set<const IBonusBearer *> visited;
	const IBonusBearer * value = bonus;
	while(value)
	{
		if(value == &expected) return true;
		if(!visited.insert(value).second) throw std::logic_error("Cyclic detached bonus-provider ancestry");
		const auto * detached = dynamic_cast<const CUnitStateDetached *>(value);
		if(!detached) return false;
		value = detached->bonus;
	}
	return false;
}
const battle::Unit * battle::CUnitStateDetached::completeOwnedFormSource() const
{
	std::set<const CUnitStateDetached *> visited;
	const CUnitStateDetached * context = this;
	while(context)
	{
		if(const auto * view = context->projectedBonusProvider())
		{
			const auto * source = view->completeOwnedFormSource();
			if(source && (view->evaluatedUnit() != context || source != context))
				throw std::logic_error("Owned complete-form source has a mismatched evaluated identity");
			return source;
		}
		const auto * next = dynamic_cast<const CUnitStateDetached *>(context->unitBonusProvider());
		if(!next) return nullptr; // Legacy source: no allocation or changed descriptor lookup.
		if(!visited.insert(context).second) throw std::logic_error("Cyclic detached form-provider ancestry");
		context = next;
	}
	return nullptr;
}
