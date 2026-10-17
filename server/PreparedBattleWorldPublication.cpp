/*
 * PreparedBattleWorldPublication.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "PreparedBattleWorldPublication.h"
#include "BattleUnitsChangedInput.h"
#include "../lib/networkPacks/PacksForClientBattle.h"
#include "CVCMIServer.h"
#include "CGameHandler.h"
#include "../lib/battle/PreparedBattleWorldCommit.h"
#include "../lib/bonuses/NativeBonusGraphAdmission.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/serializer/PreparedGameConnectionBatch.h"

PreparedBattleWorldPublication::PreparedBattleWorldPublication(CVCMIServer & server,
	BattleUnitsChanged & packet)
	: server(server), authority(server.gh), originalWorld(authority->gs.get()), caller(packet),
	  callerInput(std::make_unique<const BattleUnitsChanged>(packet))
{
	auto actualWorld = authority->gs;
	std::unique_lock state(CGameState::mutex);
	auto preparation = std::make_unique<NativeBonusGraphPreparation>();
	// State -> graph -> serializer/endpoint. No endpoint->state/graph acquisition.
	recipients = server.prepareBattlePublication(packet);
	world = std::make_unique<battle::PreparedBattleWorldCommit>(std::move(actualWorld),
		std::move(state), recipients->packet(), preparation);
	// Includes writer mutations from every recipient and authority replay writer.
	// Only changedStacks JSON is writer-mutated by this exact packet's serializers.
	callerProjection = world->packet().changedStacks;
}

PreparedBattleWorldPublication::~PreparedBattleWorldPublication() = default;

const battle::ProjectedBattleWorldArena & PreparedBattleWorldPublication::projected() const
{
	return world->projected();
}

void PreparedBattleWorldPublication::prepare(const TNodesVector & mutableNodes,
	const TCNodesVector & readOnlyNodes)
{
	world->prepare(mutableNodes, readOnlyNodes);
}

bool PreparedBattleWorldPublication::ready() const noexcept
{
	return !consumed && sameBattleUnitsChangedInput(caller, *callerInput)
		&& server.gh == authority && authority->gs.get() == originalWorld
		&& recipients->ready() && world->readyToCommit();
}

bool PreparedBattleWorldPublication::commit() noexcept
{
	if(!ready() || !world->commitSimulation())
		return false;
	// CAS and all simulation/replay tails succeeded. No further readiness check,
	// allocation, query, lock, I/O, callback or owned destruction may occur here.
	static_assert(noexcept(caller.changedStacks.swap(callerProjection)));
	caller.changedStacks.swap(callerProjection);
	recipients->acceptPrepared();
	consumed = true;
	return true;
}
