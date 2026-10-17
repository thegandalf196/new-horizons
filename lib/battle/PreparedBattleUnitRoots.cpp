/*
 * PreparedBattleUnitRoots.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "PreparedBattleUnitRootsState.h"
#include "BattleInfo.h"
#include "../bonuses/PreparedBonusGraphStorage.h"
#include <stdexcept>

battle::PreparedBattleUnitRoots::Data::Data(BattleInfo & destination, ProjectedBattleWorldArena::Data & arena)
	: destination(destination), arena(arena), origin(*arena.rootOrigin)
{
	if(&destination != &arena.source)
		throw std::invalid_argument("Prepared roots require the arena's exact battle owner");
	arena.requireUsable();
	if(!rootsUnchanged())
		throw std::runtime_error("Prepared roots changed since arena binding");
	occurrences.reserve(arena.slots.size());
	added.reserve(arena.slots.size() - origin.size);
	for(size_t i = 0; i < arena.slots.size(); ++i)
	{
		auto & slot = *arena.slots[i];
		Occurrence occurrence{&slot, slot.original, slot.state.get()};
		occurrences.push_back(occurrence);
		if(i < origin.size)
			continue;
		if(!slot.creationIdentity || captureIdentity(*slot.original) != *slot.creationIdentity
			|| slot.owned.get() != slot.original || slot.creationIdentity->battle
			|| slot.creationIdentity->environment || slot.creationIdentity->base || slot.creationIdentity->hypothetical)
			throw std::runtime_error("Prepared ADD root lost its cold creation identity");
		added.push_back({occurrence, slot.creationIdentity.get(),
			std::make_unique<PreparedStackStateTransfer>(*slot.original, *slot.state, *arena.graph)});
	}
	if(!added.empty())
		replacement.resize(occurrences.size());
}

bool battle::PreparedBattleUnitRoots::Data::rootsUnchanged() const noexcept
{
	if(destination.stacks.data() != origin.data || destination.stacks.size() != origin.size
		|| destination.stacks.capacity() != origin.capacity || arena.slots.size() < origin.size)
		return false;
	for(size_t i = 0; i < origin.size; ++i)
		if(destination.stacks[i].get() != origin.occurrences[i].object
			|| captureIdentity(*destination.stacks[i]) != origin.occurrences[i])
			return false;
	return true;
}

bool battle::PreparedBattleUnitRoots::Data::occurrencesUnchanged() const noexcept
{
	if(arena.slots.size() != occurrences.size())
		return false;
	for(size_t i = 0; i < occurrences.size(); ++i)
	{
		const auto & expected = occurrences[i];
		const auto & slot = *arena.slots[i];
		if(&slot != expected.slot || slot.original != expected.original || slot.state.get() != expected.state
			|| (i < origin.size && (slot.owned || slot.original != origin.occurrences[i].object)))
			return false;
	}
	for(const auto & value : added)
	{
		const auto & slot = *value.occurrence.slot;
		if(slot.owned.get() != value.occurrence.original || slot.creationIdentity.get() != value.creationIdentity
			|| captureIdentity(*slot.original) != *value.creationIdentity || !value.values->readyToCommit())
			return false;
	}
	return true;
}

battle::PreparedBattleUnitRoots::PreparedBattleUnitRoots(BattleInfo & destination, ProjectedBattleWorldArena & arena)
	: impl(std::make_unique<Data>(destination, *arena.impl))
{
	if(!readyToCommit())
		throw std::runtime_error("Prepared root participant is stale");
}

battle::PreparedBattleUnitRoots::~PreparedBattleUnitRoots() = default;

bool battle::PreparedBattleUnitRoots::readyToCommit() const noexcept
{
	return !impl->consumed && !impl->arena.failed && !impl->arena.rootStorageTransferred
		&& PreparedBonusGraphStorage::currentGeneration() == impl->origin.generation
		&& impl->rootsUnchanged() && impl->occurrencesUnchanged();
}

bool battle::PreparedBattleUnitRoots::commit() noexcept
{
	if(!readyToCommit())
		return false;
	exchangePreparedStorage();
	return true;
}

void battle::PreparedBattleUnitRoots::exchangePreparedStorage() noexcept
{
	for(auto & value : impl->added)
	{
		value.values->exchangePrepared();
		auto & stack = *value.occurrence.original;
		stack.battle = &impl->destination;
		stack.bindStateEnvironment(&stack);
	}
	if(!impl->added.empty())
	{
		for(size_t i = 0; i < impl->origin.size; ++i)
			impl->replacement[i].swap(impl->destination.stacks[i]);
		for(size_t i = 0; i < impl->added.size(); ++i)
			impl->replacement[impl->origin.size + i].swap(impl->added[i].occurrence.slot->owned);
		impl->destination.stacks.swap(impl->replacement);
	}
	impl->consumed = true;
	impl->arena.rootStorageTransferred = true;
}
