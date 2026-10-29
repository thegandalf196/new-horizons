/*
 * TownSiegeArmy.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "CGTownInstance.h"
#include "CGHeroInstance.h"
#include "../callback/IGameEventCallback.h"
#include "../entities/hero/NewHorizonsCapabilityRules.h"
#include "../entities/hero/NewHorizonsLeadership.h"
#include "../networkPacks/StackLocation.h"

#include <algorithm>
#include <array>
#include <limits>

namespace
{
void mergeLeadershipGarrison(const CGTownInstance & town, const CGHeroInstance & hero,
	IGameEventCallback & gameEvents)
{
	std::array<bool, GameConstants::ARMY_SIZE> considered{};
	const auto sourceSlots = town.stacksCount();
	for(int sourceIndex = 0; sourceIndex < sourceSlots; ++sourceIndex)
	{
		SlotID sourceSlot;
		ui64 bestPower = 0;
		for(const auto & [slot, stack] : town.Slots())
		{
			if(considered[slot.getNum()])
				continue;
			auto power = stack->getPower();
			const auto matching = hero.getSlotFor(stack->getCreatureID());
			if(matching.validSlot() && hero.hasStackAtSlot(matching))
				power += hero.getStack(matching).getPower();
			if(!sourceSlot.validSlot() || power > bestPower)
			{
				sourceSlot = slot;
				bestPower = power;
			}
		}
		if(!sourceSlot.validSlot())
			break;
		// A blocked source or a displaced weaker stack must not starve the rest.
		considered[sourceSlot.getNum()] = true;
		const auto creature = town.getStack(sourceSlot).getCreatureID();
		const auto capacity = hero.getLeadershipSlotCapacity(creature);
		bool rejected = false;
		for(int transferIndex = 0; transferIndex < GameConstants::ARMY_SIZE
			&& town.hasStackAtSlot(sourceSlot); ++transferIndex)
		{
			const int64_t reserve = town.needsLastStack() && town.stacksCount() == 1 ? 1 : 0;
			const int64_t available = town.getStackCount(sourceSlot) - reserve;
			if(available <= 0)
				break;
			const auto destination = newHorizonsHeroes::recruitmentSlot(&hero, creature, available);
			if(!destination.validSlot())
				break;
			const int64_t existing = hero.hasStackAtSlot(destination)
				? hero.getStackCount(destination) : 0;
			const int64_t maximum = capacity ? capacity->maximum : std::numeric_limits<TQuantity>::max();
			const auto count = std::min(available, std::max<int64_t>(0, maximum - existing));
			if(count <= 0)
				break;
			if(!gameEvents.moveStack(StackLocation(town.id, sourceSlot),
				StackLocation(hero.id, destination), static_cast<TQuantity>(count)))
			{
				rejected = true;
				break;
			}
		}
		if(rejected || !town.hasStackAtSlot(sourceSlot))
			continue;
		const auto & remaining = town.getStack(sourceSlot);
		if(capacity && !capacity->accepts(remaining.getCount()))
			continue;
		SlotID weakest;
		auto weakestPower = remaining.getPower();
		for(const auto & [slot, stack] : hero.Slots())
			if(stack->getPower() < weakestPower)
			{
				weakest = slot;
				weakestPower = stack->getPower();
			}
		if(weakest.validSlot())
			gameEvents.swapStacks(StackLocation(town.id, sourceSlot), StackLocation(hero.id, weakest));
	}
}
}

void CGTownInstance::mergeGarrisonOnSiege(IGameEventCallback & gameEvents) const
{
	const auto * hero = getVisitingHero();
	if(!hero)
		return;
	if(std::ranges::any_of(Slots(), [hero](const auto & entry)
	{
		return hero->getLeadershipSlotCapacity(entry.second->getCreatureID()).has_value();
	}))
	{
		mergeLeadershipGarrison(*this, *hero, gameEvents);
		return;
	}

	// Preserve the original uncapped assembly order for historical saves.
	auto getWeakestStackSlot = [&](ui64 powerLimit)
	{
		SlotID weakestSlot;

		for ( const auto & pair : getVisitingHero()->stacks)
		{
			if(powerLimit > pair.second->getPower() &&
			   (weakestSlot == SlotID() || pair.second->getPower() < getVisitingHero()->getStack(weakestSlot).getPower()))
			{
				weakestSlot = pair.first;
			}
		}

		return weakestSlot;
	};

	auto count = static_cast<int>(stacks.size());

	for(int i = 0; i < count; i++)
	{
		const auto & pair = *vstd::maxElementByFun(stacks, [&](const auto & elem)
		{
			ui64 power = elem.second->getPower();
			auto dst = getVisitingHero()->getSlotFor(elem.second->getCreatureID());
			if(dst.validSlot() && getVisitingHero()->hasStackAtSlot(dst))
				power += getVisitingHero()->getStack(dst).getPower();

			return power;
		});
		auto dst = getVisitingHero()->getSlotFor(pair.second->getCreatureID());
		if(dst.validSlot())
			gameEvents.moveStack(StackLocation(id, pair.first), StackLocation(getVisitingHero()->id, dst), -1);
		else
		{
			dst = getWeakestStackSlot(static_cast<int>(pair.second->getPower()));
			if(dst.validSlot())
				gameEvents.swapStacks(StackLocation(id, pair.first), StackLocation(getVisitingHero()->id, dst));
		}
	}
}
