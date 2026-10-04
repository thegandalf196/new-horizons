/*
 * NewHorizonsFriendlyFire.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "NewHorizonsFriendlyFire.h"

#include "CSpell.h"
#include "effects/Effect.h"

#include "../CStack.h"
#include "../battle/CBattleInfoCallback.h"

#include <algorithm>
#include <map>
#include <string_view>

namespace newHorizonsFriendlyFire
{
namespace
{
constexpr std::string_view handOfFateSpellKey = "new-horizons:handOfFate";

using StackByID = std::map<uint32_t, const CStack *>;

void addIfFriendly(StackByID & result, const spells::Mechanics & mechanics, const battle::Unit * unit)
{
	if(!unit || !unit->alive())
		return;

	const auto * battle = mechanics.battle();
	if(!battle || battle->battleGetOwner(unit) != mechanics.getCasterColor())
		return;

	if(const auto * stack = battle->battleGetStackByID(unit->unitId(), false))
		result.emplace(stack->unitId(), stack);
}

void addDirectDamageRecipients(StackByID & result, const spells::Mechanics & mechanics,
	const spells::effects::Effect & effect, const spells::Target & filtered)
{
	if(filtered.empty())
		return;

	// getHealthChange is the same pure prediction used by spell damage displays.
	// Evaluate prefixes so chain effects retain the target index/falloff encoded in
	// the canonical ordered transform, and only include a stack if its own
	// incremental result can cause HP or creature loss.
	spells::Target prefix;
	spells::effects::SpellEffectValue previous;
	for(const auto & destination : filtered)
	{
		prefix.push_back(destination);
		const auto current = effect.getHealthChange(&mechanics, prefix);
		if(current.hpDelta < previous.hpDelta || current.unitsDelta < previous.unitsDelta)
			addIfFriendly(result, mechanics, destination.unitValue);
		previous = current;
	}
}

void addHandOfFateCollateralCandidates(StackByID & result, const spells::Mechanics & mechanics,
	const spells::effects::Effect & effect, const spells::Target & aimPoint,
	const spells::Target & spellTarget)
{
	const auto transformed = effect.transformTarget(&mechanics, aimPoint, spellTarget);
	const auto primaryTargets = effect.filterTarget(&mechanics, transformed);
	if(primaryTargets.empty())
		return;

	const auto * battle = mechanics.battle();
	if(!battle)
		return;

	for(size_t primaryIndex = 0; primaryIndex < primaryTargets.size(); ++primaryIndex)
	{
		spells::Target prefix(primaryTargets.begin(), primaryTargets.begin() + primaryIndex + 1);
		spells::Target previousPrefix(primaryTargets.begin(), primaryTargets.begin() + primaryIndex);
		const auto currentChange = effect.getHealthChange(&mechanics, prefix);
		const auto previousChange = effect.getHealthChange(&mechanics, previousPrefix);
		const int64_t primaryDamage = std::max<int64_t>(0, previousChange.hpDelta - currentChange.hpDelta);
		const int64_t spillBase = primaryDamage / 2;
		if(spillBase <= 0)
			continue;

		const auto * primary = primaryTargets[primaryIndex].unitValue;
		if(!primary)
			continue;

		spells::Target candidates;
		for(const auto * candidate : battle->battleGetAllUnits(false))
		{
			if(!candidate || candidate->unitId() == primary->unitId() || !candidate->alive()
				|| !candidate->getPosition().isValid() || candidate->isTurret())
				continue;

			candidates.emplace_back(candidate);
		}

		// Hand of Fate draws from this candidate pool before checking spell
		// receptiveness. filterTarget reuses the configured damage effect's exact
		// validity/immunity filter without invoking the branch's resistance roll.
		for(const auto & destination : effect.filterTarget(&mechanics, candidates))
		{
			const auto * candidate = destination.unitValue;
			if(candidate && mechanics.adjustRecipientDamage(candidate, spillBase) > 0)
				addIfFriendly(result, mechanics, candidate);
		}
	}
}

}

std::vector<const CStack *> potentialFriendlyDamageTargets(const spells::Mechanics & mechanics,
	const spells::Target & aimPoint)
{
	StackByID result;
	const auto * spell = mechanics.getSpell();
	const auto casterSide = mechanics.getCasterSide();
	if(!spell || !spell->isDamage() || !mechanics.getHeroCaster() || !mechanics.usesNewHorizonsMagicV3()
		|| !mechanics.battle() || (casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER))
		return {};

	const auto spellTarget = mechanics.canonicalizeTarget(aimPoint);
	mechanics.forEachEffect([&](const spells::effects::Effect & effect)
	{
		if(effect.indirect)
			return false;

		const auto transformed = effect.transformTarget(&mechanics, aimPoint, spellTarget);
		const auto filtered = effect.filterTarget(&mechanics, transformed);
		addDirectDamageRecipients(result, mechanics, effect, filtered);
		return false;
	});

	// This one scripted damage effect has an additional random collateral hit
	// after each primary hit. Its recipient is selected from every living,
	// positioned, non-turret stack before defenses are checked. Include only
	// currently receptive candidates that would retain positive damage; do not
	// draw or predict the random recipient.
	if(spell->getJsonKey() == handOfFateSpellKey)
	{
		mechanics.forEachEffect([&](const spells::effects::Effect & effect)
		{
			if(!effect.indirect)
				addHandOfFateCollateralCandidates(result, mechanics, effect, aimPoint, spellTarget);
			return false;
		});
	}

	std::vector<const CStack *> stacks;
	stacks.reserve(result.size());
	for(const auto & [unitID, stack] : result)
	{
		(void)unitID;
		stacks.push_back(stack);
	}
	return stacks;
}

ConfirmationGate::ConfirmationGate(ConfirmationSnapshot value)
	: snapshot(std::move(value))
{}

bool ConfirmationGate::confirm(const ConfirmationSnapshot & current)
{
	if(completed || snapshot != current)
		return false;
	completed = true;
	return true;
}

bool ConfirmationGate::cancel()
{
	if(completed)
		return false;
	completed = true;
	return true;
}

bool ConfirmationGate::isPending() const
{
	return !completed;
}

const ConfirmationSnapshot & ConfirmationGate::getSnapshot() const
{
	return snapshot;
}

}
