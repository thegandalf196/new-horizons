/*
 * PreparedBattleWorldCommit.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "PreparedBattleWorldCommitState.h"
#include "BattleInfo.h"
#include <stdexcept>

battle::PreparedBattleWorldCommit::Data::Data(std::shared_ptr<CGameState> world,
	std::unique_lock<std::shared_mutex> access, const BattleUnitsChanged & packet,
	std::unique_ptr<NativeBonusGraphPreparation> preparation,
	std::unique_ptr<PreparedReplayAppend> adoptedReplay)
	: world(std::move(world)), access(std::move(access)), preparation(std::move(preparation)),
	  thread(std::this_thread::get_id()), packet(packet)
{
	if(!this->world || !this->access.owns_lock() || this->access.mutex() != &CGameState::mutex)
		throw std::invalid_argument("Prepared world requires the real world and held state mutex");
	if(!this->preparation || !this->preparation->activeOnThisThread())
		throw std::invalid_argument("Prepared world requires its active native preparation");
	rootLifetime = BattleRootLifetime::retain(this->world);
	battleData = this->world->currentBattles.data();
	battleCapacity = this->world->currentBattles.capacity();
	for(const auto & battle : this->world->currentBattles)
	{
		if(!battle)
			throw std::invalid_argument("Prepared world requires nonnull battle occurrences");
		battleOccurrences.push_back(battle.get());
	}
	original = this->world->getBattle(packet.battleID);
	if(!original)
		throw std::invalid_argument("Prepared world requires its existing packet battle");
	day = this->world->day; // only an existing initialized battle world supplies this input
	// Same pre-packet replay snapshot/encoding owner as CGameState::apply.
	// Finish this BEFORE any arena binding or projected packet query/event.
	if(adoptedReplay)
		replay = std::move(adoptedReplay);
	else
		replay = prepareBattleReplayAppend(*this->world, this->packet);
	arena = std::make_unique<ProjectedBattleWorldArena>(*original);
	arena->applyPacket(this->packet);
}

bool battle::PreparedBattleWorldCommit::Data::sameWorld() const noexcept
{
	if(!rootLifetime->admitted() || !access.owns_lock() || access.mutex() != &CGameState::mutex || thread != std::this_thread::get_id()
		|| world->day != day || world->currentBattles.data() != battleData
		|| world->currentBattles.capacity() != battleCapacity || world->currentBattles.size() != battleOccurrences.size())
		return false;
	for(size_t i = 0; i < battleOccurrences.size(); ++i)
		if(world->currentBattles[i].get() != battleOccurrences[i])
			return false;
	return world->getBattle(packet.battleID) == original;
}

battle::PreparedBattleWorldCommit::~PreparedBattleWorldCommit() = default;

const battle::ProjectedBattleWorldArena & battle::PreparedBattleWorldCommit::projected() const
{
	if(impl->phase != Data::Phase::BUILDING || !impl->sameWorld())
		throw std::logic_error("Prepared world no longer admits preparation queries");
	return *impl->arena;
}

const BattleUnitsChanged & battle::PreparedBattleWorldCommit::packet() const noexcept { return impl->packet; }

void battle::PreparedBattleWorldCommit::prepare(const TNodesVector & authority)
{
	prepare(authority, {});
}

void battle::PreparedBattleWorldCommit::prepare(const TNodesVector & authority, const TCNodesVector & readOnly)
{
	if(impl->phase != Data::Phase::BUILDING || !impl->sameWorld())
		throw std::logic_error("Prepared world is stale or already consumed");
	try
	{
		auto & data = *impl->arena->impl;
		// Root provenance must refuse before following any borrowed original Slot.
		impl->roots = std::make_unique<PreparedBattleUnitRoots>(*impl->original, *impl->arena);
		impl->values.reserve(data.rootOrigin->size);
		for(size_t i = 0; i < data.rootOrigin->size; ++i)
		{
			auto & slot = *data.slots[i];
			impl->values.push_back(std::make_unique<PreparedStackStateTransfer>(*slot.original, *slot.state, *data.graph));
		}
		impl->sidecars = std::make_unique<PreparedBattleUnitSidecars>(*impl->original, *impl->arena);
		// Existing friend-owned arena storage; no arena API/AI-owned file edit.
		data.finalizer = std::make_unique<PreparedBonusGraphFinalization>(*data.graph, authority, readOnly);
		impl->phase = Data::Phase::PREPARED;
		if(!readyToCommit())
			throw std::runtime_error("Prepared world changed before graph reservation");
	}
	catch(...)
	{
		impl->phase = Data::Phase::FAILED;
		impl->arena->impl->abandon();
		throw;
	}
}

bool battle::PreparedBattleWorldCommit::readyToCommit() const noexcept
{
	if(impl->phase != Data::Phase::PREPARED || !impl->sameWorld()
		|| !impl->roots->readyToCommit() || !impl->sidecars->readyToCommit()
		|| !impl->replay->readyToCommit() || !impl->arena->graphStorageReady())
		return false;
	for(const auto & value : impl->values)
		if(!value->readyToCommit())
			return false;
	return true;
}

bool battle::PreparedBattleWorldCommit::commitSimulation() noexcept
{
	auto admission = impl->rootLifetime->tryCommit();
	if(!admission || !readyToCommit())
		return false;
	// Graph commit is the LAST refusal gate; its CAS is the first irreversible
	// write. No readiness/generation/query/callback follows successful reservation.
	if(!impl->arena->impl->finalizer->commit())
		return false;
	for(auto & value : impl->values)
		value->exchangePrepared();
	impl->roots->exchangePreparedStorage();
	impl->sidecars->exchangePreparedStorage();
	impl->replay->exchangePreparedStorage();
	impl->phase = Data::Phase::COMMITTED;
	return true;
}
