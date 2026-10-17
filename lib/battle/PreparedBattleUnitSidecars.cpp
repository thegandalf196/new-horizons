/*
 * PreparedBattleUnitSidecars.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "PreparedBattleUnitSidecars.h"
#include "ProjectedBattleWorldArenaState.h"
#include "BattleUnitSidecarFields.h"
#include <stdexcept>
#include <type_traits>

auto battle::BattleUnitSidecarFields::projected(const ProjectedBattleWorldArena & arena)
{
	const auto & data = *arena.impl;
	return std::tie(data.orders[BattleSide::ATTACKER], data.reduced[BattleSide::ATTACKER],
		data.chainUsed[BattleSide::ATTACKER], data.sparkUsed[BattleSide::ATTACKER],
		data.sideValues.state(BattleSide::ATTACKER).bloodrageDamagePercent,
		data.orders[BattleSide::DEFENDER], data.reduced[BattleSide::DEFENDER],
		data.chainUsed[BattleSide::DEFENDER], data.sparkUsed[BattleSide::DEFENDER],
		data.sideValues.state(BattleSide::DEFENDER).bloodrageDamagePercent,
		data.crisis.used, data.crisis.pending, data.destroyedUnits, data.firstBlood);
}

auto battle::BattleUnitSidecarFields::projectedInputs(const ProjectedBattleWorldArena & arena)
{
	const auto & data = *arena.impl;
	// The arena deliberately never changes the legacy command marker or terrain.
	// These are witnesses only, not a rule query or mutable live fallback.
	const auto & source = data.source;
	return std::tie(source.sides[BattleSide::ATTACKER].activeOrder, data.allowances[BattleSide::ATTACKER],
		data.sideValues.inputs(BattleSide::ATTACKER).bloodrageRank,
		data.sideValues.inputs(BattleSide::ATTACKER).bloodrageCap,
		source.sides[BattleSide::DEFENDER].activeOrder, data.allowances[BattleSide::DEFENDER],
		data.sideValues.inputs(BattleSide::DEFENDER).bloodrageRank,
		data.sideValues.inputs(BattleSide::DEFENDER).bloodrageCap,
		data.crisis.returns, data.seize, data.battleId, data.round, source.terrainType, source.battlefieldType);
}

namespace
{
static_assert(std::tuple_size_v<battle::BattleUnitSidecarValues> == 14);
static_assert(std::tuple_size_v<battle::BattleUnitSidecarInputs> == 14);
static_assert(std::is_same_v<battle::BattleUnitSidecarValues, decltype(battle::copyBattleSidecarTuple(
	battle::BattleUnitSidecarFields::projected(std::declval<const battle::ProjectedBattleWorldArena &>())))>);
static_assert(std::is_same_v<battle::BattleUnitSidecarInputs, decltype(battle::copyBattleSidecarTuple(
	battle::BattleUnitSidecarFields::projectedInputs(std::declval<const battle::ProjectedBattleWorldArena &>())))>);

template<typename T> void exchangeField(T & destination, T & prepared) noexcept
{
	if constexpr(std::is_trivially_copyable_v<T>)
	{
		static_assert(std::is_nothrow_swappable_v<T> && std::is_trivially_destructible_v<T>);
		using std::swap;
		swap(destination, prepared);
	}
	else
	{
		static_assert(std::allocator_traits<typename T::allocator_type>::is_always_equal::value);
		static_assert(noexcept(destination.swap(prepared)));
		destination.swap(prepared);
	}
}

template<typename References, typename Values, size_t... Index>
void exchangeChanged(References references, Values & values,
	const std::array<bool, sizeof...(Index)> & changed, std::index_sequence<Index...>) noexcept
{
	// Built-in comma fold fixes order; each concrete field swap is no-throw.
	((changed[Index] ? exchangeField(std::get<Index>(references), std::get<Index>(values)) : void()), ...);
}

template<typename Values, size_t... Index>
auto changes(const Values & original, const Values & prepared, std::index_sequence<Index...>)
{
	return std::array<bool, sizeof...(Index)>{(std::get<Index>(original) != std::get<Index>(prepared))...};
}
}

struct battle::PreparedBattleUnitSidecars::Data
{
	BattleInfo & destination;
	const ProjectedBattleWorldArena & arena;
	BattleUnitSidecarOrigin original;
	BattleUnitSidecarValues prepared;
	static constexpr auto indices = std::make_index_sequence<std::tuple_size_v<BattleUnitSidecarValues>>{};
	std::array<bool, std::tuple_size_v<BattleUnitSidecarValues>> changed;
	bool consumed = false;
	Data(BattleInfo & destination, const ProjectedBattleWorldArena & arena,
		const BattleUnitSidecarOrigin & original)
		: destination(destination), arena(arena), original(original),
		prepared(copyBattleSidecarTuple(BattleUnitSidecarFields::projected(arena))),
		changed(changes(original.values, prepared, indices)) {}
};

battle::PreparedBattleUnitSidecars::PreparedBattleUnitSidecars(
	BattleInfo & destination, const ProjectedBattleWorldArena & arena)
{
	arena.impl->requireUsable();
	if(&arena.impl->source != &destination || !arena.impl->sidecarOrigin)
		throw std::invalid_argument("Sidecar transfer requires the arena's exact original owner");
	const auto & original = *arena.impl->sidecarOrigin;
	if(BattleUnitSidecarFields::writable(std::as_const(destination)) != original.values
		|| BattleUnitSidecarFields::inputs(std::as_const(destination)) != original.inputs)
		throw std::logic_error("Original sidecars changed after arena binding");
	// Same typed validator used by the packet program; all possible allocation,
	// callbacks and failures occur in preparation, never in storage exchange.
	arena.impl->validateCrisisForStorage(arena.impl->crisis);
	impl = std::make_unique<Data>(destination, arena, original);
	if(!readyToCommit())
		throw std::logic_error("Projected sidecars changed or do not preserve packet input witnesses");
}

battle::PreparedBattleUnitSidecars::~PreparedBattleUnitSidecars() = default;

bool battle::PreparedBattleUnitSidecars::readyToCommit() const noexcept
{
	if(!impl || impl->consumed || impl->arena.impl->failed)
		return false;
	try
	{
		return BattleUnitSidecarFields::writable(std::as_const(impl->destination)) == impl->original.values
			&& BattleUnitSidecarFields::inputs(std::as_const(impl->destination)) == impl->original.inputs
			&& BattleUnitSidecarFields::projected(impl->arena) == impl->prepared
			&& BattleUnitSidecarFields::projectedInputs(impl->arena) == impl->original.inputs
			&& impl->arena.impl->primaryElemental == impl->original.primaryElemental;
	}
	catch(...) { return false; } // noexcept refusal; no writes or partial exchange
}

bool battle::PreparedBattleUnitSidecars::commit() noexcept
{
	if(!readyToCommit())
		return false;
	exchangePreparedStorage();
	return true;
}

void battle::PreparedBattleUnitSidecars::exchangePreparedStorage() noexcept
{
	exchangeChanged(BattleUnitSidecarFields::writable(impl->destination), impl->prepared,
		impl->changed, Data::indices);
	impl->consumed = true;
}
