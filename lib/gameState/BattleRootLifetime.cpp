/*
 * BattleRootLifetime.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleRootLifetime.h"
#include "CGameState.h"
#include <stdexcept>
#include <limits>

BattleRootLifetime::Pin::Pin(std::shared_ptr<CGameState> world, BattleRootLifetime & owner)
	: world(std::move(world)), owner(owner), lock(owner.mutex)
{
	std::lock_guard state(owner.pinStateMutex);
	if(owner.mutations || owner.retirements.load() || owner.permanentlyRetired.load())
		throw std::logic_error("Cannot prepare borrowed battle roots inside native root mutation");
	if(owner.pins == std::numeric_limits<size_t>::max())
		throw std::overflow_error("Too many prepared battle root pins");
	owner.pinThread = std::this_thread::get_id();
	++owner.pins;
}

BattleRootLifetime::Pin::~Pin()
{
	std::lock_guard state(owner.pinStateMutex);
	--owner.pins; // Lock retires next; shared world retires LAST.
	if(!owner.pins)
		owner.pinThread = {};
}

bool BattleRootLifetime::Pin::admitted() const noexcept
{
	return thread == std::this_thread::get_id() && !owner.retirements.load() && !owner.permanentlyRetired.load();
}

BattleRootLifetime::Mutation::Mutation(std::shared_ptr<CGameState> world, BattleRootLifetime & owner)
	: world(std::move(world)), owner(owner), lock(owner.mutex)
{
	{
		std::lock_guard state(owner.pinStateMutex);
		if(owner.pins)
			throw std::logic_error("Prepared battle roots forbid reentrant native retirement");
	}
	if(owner.mutations == std::numeric_limits<size_t>::max())
		throw std::overflow_error("Too many native battle root mutations");
	++owner.mutations; // Native mutation nesting remains legal outside a pin.
}

BattleRootLifetime::Mutation::~Mutation()
{
	--owner.mutations;
}

std::unique_ptr<BattleRootLifetime::Pin> BattleRootLifetime::retain(std::shared_ptr<CGameState> world)
{
	if(!world)
		throw std::invalid_argument("Battle root pin requires its real shared world");
	auto & owner = world->battleRootLifetime;
	return std::unique_ptr<Pin>(new Pin(std::move(world), owner));
}

BattleRootLifetime::Retirement::Retirement(std::shared_ptr<CGameState> world,
	BattleRootLifetime & owner, RetirementMode mode)
	: world(std::move(world)), owner(owner)
{
	// Linearize closing admission WITH commit admission, never before its lock.
	// No root-lock wait, callback, GUI teardown or join occurs in this scope.
	std::lock_guard state(owner.pinStateMutex);
	if(owner.pins && owner.pinThread == std::this_thread::get_id())
		throw std::logic_error("Cannot retire roots reentrantly during prepared access");
	const auto count = owner.retirements.load();
	if(count == std::numeric_limits<size_t>::max())
		throw std::overflow_error("Too many concurrent battle retirements");
	owner.retirements.store(count + 1);
	if(mode == RetirementMode::PERMANENT)
		owner.permanentlyRetired.store(true);
}

BattleRootLifetime::Retirement::~Retirement()
{
	std::lock_guard state(owner.pinStateMutex);
	owner.retirements.fetch_sub(1);
}

BattleRootLifetime::Mutation BattleRootLifetime::Retirement::mutate()
{
	return Mutation(world, owner);
}

BattleRootLifetime::Retirement BattleRootLifetime::retire(std::shared_ptr<CGameState> world, RetirementMode mode)
{
	if(!world)
		throw std::invalid_argument("Battle root retirement requires its real shared world");
	auto & owner = world->battleRootLifetime;
	return Retirement(std::move(world), owner, mode);
}

BattleRootLifetime::Mutation BattleRootLifetime::mutate()
{
	return Mutation({}, *this);
}
