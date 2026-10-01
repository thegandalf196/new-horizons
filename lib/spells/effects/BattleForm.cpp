/*
 * BattleForm.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "BattleForm.h"

#include "../ISpellMechanics.h"

#include "../../battle/AccessibilityInfo.h"
#include "../../battle/CBattleInfoCallback.h"
#include "../../battle/CUnitState.h"
#include "../../battle/Unit.h"
#include "../../entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../networkPacks/PacksForClientBattle.h"
#include "../../json/JsonNode.h"

#include <vstd/RNG.h>

#include <cmath>
#include <limits>
#include <stdexcept>

namespace spells
{
namespace effects
{

namespace
{
constexpr int32_t DEFAULT_DURATION = 2;

using CreatureForms = std::vector<const Creature *>;

bool isBasicTarget(const Mechanics * mechanics, const battle::Unit * unit)
{
	if(!mechanics || !mechanics->battle() || !unit || !unit->alive() || !unit->isValidTarget(false))
		return false;
	if(mechanics->isSmart() && !mechanics->ownerMatches(unit))
		return false;
	if(unit->isInvincible() && mechanics->isNegativeSpell())
		return false;
	return mechanics->isReceptive(unit);
}

bool isUnsupportedProfile(const battle::Unit * unit)
{
	return unit && (unit->isClone() || unit->getPhantomInitialIntegrity() > 0);
}

CreatureForms getForms(const Mechanics * mechanics, const battle::Unit * unit)
{
	CreatureForms result;
	if(!mechanics || !mechanics->battle() || !mechanics->creatures() || !unit)
		return result;

	const auto category = mechanics->battle()->battleGetCreatureCategory(unit->creatureId());
	if(!category)
		return result;

	mechanics->creatures()->forEach([&](const Creature * creature, bool & stop)
	{
		(void)stop;
		if(!creature || creature->getBaseHitPoints() <= 0)
			return;

		const auto candidateCategory = mechanics->battle()->battleGetCreatureCategory(creature->getId());
		if(candidateCategory && candidateCategory->category == category->category)
			result.push_back(creature);
	});
	return result;
}

void addUnsupportedProfileProblem(Problem & problem)
{
	MetaString message;
	message.appendRawString("Battle-form spells cannot currently affect Clone or Phantom Army stacks.");
	problem.add(std::move(message), Problem::NORMAL);
}

void addNoLegalPlacementProblem(Problem & problem)
{
	MetaString message;
	message.appendRawString("There is no legal battlefield position for every same-category creature form.");
	problem.add(std::move(message), Problem::NORMAL);
}
}

void BattleFormEffect::initImpl(JsonNode data)
{
	if(!data.isStruct())
		throw std::runtime_error("Battle-form effect parameters must be an object");

	for(const auto & [key, value] : data.Struct())
	{
		if(key != "type" && key != "duration" && key != "indirect" && key != "optional")
			throw std::runtime_error("Unknown battle-form effect parameter: " + key);
	}

	const auto & type = data["type"];
	if(!type.isString() || type.String() != "core:battleForm")
		throw std::runtime_error("Battle-form effect requires type 'core:battleForm'");

	const auto & rawDuration = data["duration"];
	if(rawDuration.isNull())
	{
		duration = DEFAULT_DURATION;
		return;
	}

	if(!rawDuration.isNumber() || !std::isfinite(rawDuration.Float())
		|| std::floor(rawDuration.Float()) != rawDuration.Float()
		|| rawDuration.Float() < 1
		|| rawDuration.Float() > std::numeric_limits<int32_t>::max())
		throw std::runtime_error("Battle-form duration must be a positive 32-bit integer");

	duration = rawDuration.Integer();
}

void BattleFormEffect::adjustAffectedHexes(BattleHexArray & hexes, const Mechanics * mechanics, const Target & spellTarget) const
{
	for(const auto & destination : spellTarget)
	{
		const auto hex = destination.unitValue ? destination.unitValue->getPosition() : destination.hexValue;
		if(hex.isValid())
			hexes.insert(hex);
	}
}

bool BattleFormEffect::hasUsableForms(const Mechanics * mechanics, const battle::Unit * unit) const
{
	return !getForms(mechanics, unit).empty();
}

bool BattleFormEffect::hasLegalPlacementForEveryForm(const Mechanics * mechanics, const battle::Unit * unit) const
{
	if(!mechanics || !mechanics->battle() || !unit)
		return false;

	const auto forms = getForms(mechanics, unit);
	if(forms.empty())
		return false;

	// Free the target's current footprint, then require every distinct footprint class
	// represented in the full same-category pool to have a legal landing spot. No form is
	// removed based on its current anchor, and each board scan is done at most once per width.
	const auto accessibility = mechanics->battle()->getAccessibility(unit);
	const bool needsSingleWide = std::ranges::any_of(forms, [](const Creature * form)
	{
		return !form->isDoubleWide();
	});
	const bool needsDoubleWide = std::ranges::any_of(forms, [](const Creature * form)
	{
		return form->isDoubleWide();
	});
	const auto origin = unit->getPosition();
	const auto side = unit->unitSide();
	return (!needsSingleWide || accessibility.nearestLegalPosition(origin, false, side).has_value())
		&& (!needsDoubleWide || accessibility.nearestLegalPosition(origin, true, side).has_value());
}

bool BattleFormEffect::isSupportedTarget(const Mechanics * mechanics, const battle::Unit * unit) const
{
	return isBasicTarget(mechanics, unit)
		&& !isUnsupportedProfile(unit)
		&& hasLegalPlacementForEveryForm(mechanics, unit);
}

bool BattleFormEffect::applicableGeneral(Problem & problem, const Mechanics * mechanics) const
{
	if(!mechanics || !mechanics->battle())
		return false;

	bool unsupportedProfile = false;
	bool noLegalPlacement = false;
	for(const auto * unit : mechanics->battle()->battleGetAllUnits(false))
	{
		if(!isBasicTarget(mechanics, unit))
			continue;
		if(isUnsupportedProfile(unit))
		{
			unsupportedProfile = true;
			continue;
		}
		if(!hasUsableForms(mechanics, unit))
			continue;
		if(!hasLegalPlacementForEveryForm(mechanics, unit))
		{
			noLegalPlacement = true;
			continue;
		}
		return true;
	}

	if(noLegalPlacement)
		addNoLegalPlacementProblem(problem);
	else if(unsupportedProfile)
		addUnsupportedProfileProblem(problem);
	else
		mechanics->adaptProblem(ESpellCastProblem::NO_APPROPRIATE_TARGET, problem);
	return false;
}

bool BattleFormEffect::applicableTarget(Problem & problem, const Mechanics * mechanics, const Target & target) const
{
	bool unsupportedProfile = false;
	bool noLegalPlacement = false;
	bool noUsableForms = false;
	for(const auto & destination : target)
	{
		const auto * unit = destination.unitValue;
		if(!isBasicTarget(mechanics, unit))
			continue;
		if(isUnsupportedProfile(unit))
		{
			unsupportedProfile = true;
			continue;
		}
		if(!hasUsableForms(mechanics, unit))
		{
			noUsableForms = true;
			continue;
		}
		if(!hasLegalPlacementForEveryForm(mechanics, unit))
		{
			noLegalPlacement = true;
			continue;
		}
		return true;
	}

	if(noLegalPlacement)
		addNoLegalPlacementProblem(problem);
	else if(unsupportedProfile)
		addUnsupportedProfileProblem(problem);
	else if(noUsableForms)
		mechanics->adaptProblem(ESpellCastProblem::NO_APPROPRIATE_TARGET, problem);
	return false;
}

void BattleFormEffect::apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const
{
	if(!server || !mechanics || !mechanics->battle())
		return;

	auto * rng = server->getRNG();
	if(!rng)
	{
		server->complain("Battle-form effect has no server random generator");
		return;
	}

	for(const auto & destination : target)
	{
		const auto * unit = destination.unitValue;
		if(!isSupportedTarget(mechanics, unit))
			continue;

		auto forms = getForms(mechanics, unit);
		if(forms.empty())
			continue;

		const auto accessibility = mechanics->battle()->getAccessibility(unit);
		const auto * selectedForm = *RandomGeneratorUtil::nextItem(forms, *rng);
		const auto newPosition = accessibility.nearestLegalPosition(
			unit->getPosition(), selectedForm->isDoubleWide(), unit->unitSide());
		if(!newPosition)
		{
			server->complain("Battle-form effect found no legal position after target validation");
			continue;
		}

		auto state = unit->acquireState();
		if(!state || state->cloned || state->getPhantomInitialIntegrity() > 0 || !state->alive())
		{
			// This profile is a temporary implementation gap, reported during applicability;
			// keep this late guard so an accepted cast can never throw after spending Mana.
			server->complain("Battle-form effect cannot currently apply to Clone or Phantom Army stacks");
			continue;
		}

		try
		{
			state->beginBattleForm(selectedForm->getId(), duration);
			state->setPosition(*newPosition);
		}
		catch(const std::exception & error)
		{
			server->complain("Battle-form effect failed without changing the live unit: " + std::string(error.what()));
			continue;
		}

		BattleUnitsChanged changed;
		changed.battleID = mechanics->getBattleID();
		UnitChanges update(state->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		changed.changedStacks.push_back(std::move(update));
		server->apply(changed);
	}
}

Target BattleFormEffect::filterTarget(const Mechanics * mechanics, const Target & target) const
{
	Target result;
	for(const auto & destination : target)
		if(isBasicTarget(mechanics, destination.unitValue))
			result.emplace_back(destination.unitValue, destination.unitValue->getPosition());
	return result;
}

Target BattleFormEffect::transformTarget(const Mechanics * mechanics, const Target & aimPoint, const Target & spellTarget) const
{
	Target result;
	const auto & source = spellTarget.empty() ? aimPoint : spellTarget;
	if(!mechanics || !mechanics->battle())
		return result;

	for(const auto & destination : source)
	{
		const auto * unit = destination.unitValue;
		if(!unit && destination.hexValue.isValid())
			unit = mechanics->battle()->battleGetUnitByPos(destination.hexValue, true);
		// Preserve a selected but currently unsupported unit through target preparation. The
		// applicability checks must reject it before an accepted cast can spend resources.
		if(unit && isBasicTarget(mechanics, unit)
			&& std::ranges::none_of(result, [unit](const Destination & existing)
			{
				return existing.unitValue->unitId() == unit->unitId();
			}))
			result.emplace_back(unit, unit->getPosition());
	}
	return result;
}

}
}
