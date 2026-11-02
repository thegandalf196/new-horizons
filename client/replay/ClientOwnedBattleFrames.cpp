/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#include "../StdInc.h"
#include "ClientPacketReceiveSession.h"
#include "../CServerHandler.h"
#include "../Client.h"
#include "../GameEngine.h"
#include "GameplayReplayer.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/gameState/BattleRootLifetime.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/CBattleGameInterface.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/OwnedBattleFrameBatch.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/GameConnection.h"
#include "../../lib/UnlockGuard.h"
#ifdef NH_OWNED_PREPARATION_SIMULATION_AUDIT
#include "../../server/tests/OwnedPreparationAudit.h"
#endif
#include <shared_mutex>
#include <stdexcept>
namespace
{
template<typename T> bool sameOwner(const std::shared_ptr<T> & a, const std::shared_ptr<T> & b)
{
	return a.get() == b.get() && !a.owner_before(b) && !b.owner_before(a);
}
}
std::shared_ptr<const battle::OwnedBattleFrameBatch> ClientPacketReceiveSession::captureActiveBattleFrames(
	PlayerColor observer, BattleID battle, const std::shared_ptr<CBattleGameInterface> & controller)
{
	// Physical ownership is supplied by the ONLY caller: CClient's synchronous
	// receive dispatch, immediately BEFORE makeUnlockGuard. heldOnThisThread is
	// an additional session check, never a physical-lock certificate inside AI.
	if(!access.heldOnThisThread() || ENGINE.get() != engine
		|| !sameOwner(handler.networkConnection, connection) || !sameOwner(handler.logicConnection, logic)
		|| !client || handler.client.get() != client || !world || !logic
		|| handler.networkLagCompensator.get() != predictions || client->gameStatePtr().get() != world
		|| handler.getState() != EClientState::GAMEPLAY
		|| (handler.gameplayReplayer && handler.gameplayReplayer->isActive()))
		throw std::logic_error("Owned frame capture requires the current pre-unlock native receive call");
	access.requireEmpty();
	const auto activeId = [&]()
	{
		const auto * activation = dynamic_cast<const BattleSetActiveStack *>(activePacket);
		if(!activation || activation->battleID != battle) throw std::logic_error("Expected this actual activation packet");
		return activation->stack;
	}();
	{
		const auto registered = client->battleints.find(observer);
		if(!controller || controller->human || controller->playerID != observer || !controller->usesOwnedRecipientPreparation()
			|| registered == client->battleints.end() || !sameOwner(controller, registered->second))
			throw std::logic_error("Owned capture requires the same actual registered willing controller");
	}
	int32_t serial;
	{
		// Identity probe ONLY. Retire every world/callback/unit/pin/state borrow
		// before releasing the existing receive guard for an observation wait.
		auto probeWorld = sourceWorld.lock();
		if(!probeWorld || !sameOwner(probeWorld, client->gameStatePtr())) throw std::logic_error("Capture world changed before identity probe");
		std::unique_lock<std::shared_mutex> probeAccess(CGameState::mutex);
		auto probePin = BattleRootLifetime::retain(probeWorld);
		if(!probePin || !probePin->admitted()) throw std::logic_error("Capture source is retiring before observation");
		const auto source = client->battleCallbacks.find(observer);
		if(source == client->battleCallbacks.end() || !source->second) throw std::logic_error("Missing observation callback owner");
		const auto callback = source->second->getBattle(battle);
		const auto * native = probeWorld->getBattle(battle);
		if(!callback || !native || callback->getPlayerID() != observer || callback->getBattle() != native
			|| native->getBattleID() != battle || native->activeStack != activeId)
			throw std::logic_error("Observation activation identity mismatch");
		serial = native->getActivationSerial();
	}
	access.requireEmpty();
	bool observed;
	{
		// Operate on the ACTUAL unique_lock: owns_lock/heldOnThisThread are false
		// while waiting, and its destructor relocks before any fresh source work.
		auto unlocked = vstd::makeUnlockGuard(access.interfaceAccess);
		if(access.heldOnThisThread()) throw std::logic_error("Observation retained receive admission while unlocked");
		observed = controller->awaitOwnedPreparationObservation(battle, activeId, serial);
	}
	if(!observed) return {}; // Typed cancellation; never legacy/native actor fallback.
	if(!access.heldOnThisThread() || ENGINE.get() != engine || handler.client.get() != client
		|| !sameOwner(handler.networkConnection, connection) || !sameOwner(handler.logicConnection, logic)
		|| handler.networkLagCompensator.get() != predictions || handler.getState() != EClientState::GAMEPLAY
		|| (handler.gameplayReplayer && handler.gameplayReplayer->isActive()))
		return {}; // Shutdown/replacement raced the observation; no fresh source work.
	// Connection writeMutex is released BEFORE state/root/native admission.
	const auto capabilities = logic->captureSpecialtySerializationContext(connection);
	const auto codec = OwnedBonusCaptureCodec::fromSourceCapabilities(capabilities);
	auto retainedWorld = sourceWorld.lock();
	if(!retainedWorld || !sameOwner(retainedWorld, client->gameStatePtr()))
		throw std::logic_error("Owned frame capture world identity changed");
	std::unique_lock<std::shared_mutex> stateAccess(CGameState::mutex);
	auto pin = BattleRootLifetime::retain(retainedWorld);
	if(!pin || !pin->admitted()) throw std::logic_error("Owned frame capture source is retiring");
	const auto found = client->battleCallbacks.find(observer);
	if(found == client->battleCallbacks.end() || !found->second)
		throw std::invalid_argument("Missing registered capture observer");
	const auto registeredCallbacks = found->second;
	const auto callback = registeredCallbacks->getBattle(battle);
	const auto * native = retainedWorld->getBattle(battle);
	if(!callback || callback->getPlayerID() != observer || !native || callback->getBattle() != native
		|| native->getBattleID() != battle || native->activeStack != activeId || native->getActivationSerial() != serial)
		throw std::invalid_argument("Owned frame activation/world/callback occurrence mismatch");
	const auto requireCurrentSource = [&]()
	{
		access.requireEmpty();
		if(!pin->admitted() || ENGINE.get() != engine || handler.client.get() != client)
			throw std::logic_error("Actual source owner retired inside synchronous preparation");
		const auto * activation = dynamic_cast<const BattleSetActiveStack *>(activePacket);
		const auto currentController = client->battleints.find(observer);
		const auto currentCallbacks = client->battleCallbacks.find(observer);
		if(!access.heldOnThisThread() || !pin->admitted() || ENGINE.get() != engine || !activation || activation->battleID != battle || activation->stack != activeId
			|| !sameOwner(handler.networkConnection, connection) || !sameOwner(handler.logicConnection, logic)
			|| handler.client.get() != client || !sameOwner(retainedWorld, client->gameStatePtr())
			|| handler.networkLagCompensator.get() != predictions || handler.getState() != EClientState::GAMEPLAY
			|| (handler.gameplayReplayer && handler.gameplayReplayer->isActive())
			|| currentController == client->battleints.end() || !sameOwner(controller, currentController->second)
			|| controller->human || controller->playerID != observer || !controller->usesOwnedRecipientPreparation()
			|| currentCallbacks == client->battleCallbacks.end() || !sameOwner(registeredCallbacks, currentCallbacks->second)
			|| !sameOwner(callback, currentCallbacks->second->getBattle(battle))
			|| retainedWorld->getBattle(battle) != native || callback->getBattle() != native
			|| callback->getPlayerID() != observer
			|| native->getBattleID() != battle || native->activeStack != activeId
			|| native->getActivationSerial() != serial)
			throw std::logic_error("Owned preparation source/controller/activation changed inside actual receive lease");
	};
	requireCurrentSource();
#ifdef NH_OWNED_PREPARATION_SIMULATION_AUDIT
	ownedPreparationAudit::beforeClientCapture(*callback, activeId, serial, codec.version());
#endif
	auto result = battle::OwnedBattleFrameCapture::capture(*native, *callback, activeId, codec);
	(void)result->frame(activeId); // Exact active occurrence must be in the authorized candidate universe.
	// Existing general HB metadata is captured here as well, never through a
	// deferred native callback after the actual interface lock is released.
	controller->prepareOwnedRecipientDecision(callback, result);
#ifdef NH_OWNED_PREPARATION_SIMULATION_AUDIT
	ownedPreparationAudit::afterClientCapture(*callback, activeId, serial, codec.version());
#endif
	requireCurrentSource();
	return result; // All raw borrows/metadata copies finish before pin/state retirement.
}

bool ClientPacketReceiveSession::observeAfterOwnedDecision(
	const std::shared_ptr<const battle::OwnedBattleFrameBatch> & frames,
	const std::shared_ptr<CBattleGameInterface> & controller)
{
#ifndef NH_OWNED_PREPARATION_SIMULATION_AUDIT
	return true; // Production has no test observer or imported source evidence.
#else
	if(!frames || !controller || !access.heldOnThisThread() || ENGINE.get() != engine || handler.client.get() != client
		|| !sameOwner(handler.networkConnection, connection) || !sameOwner(handler.logicConnection, logic)
		|| handler.networkLagCompensator.get() != predictions || handler.getState() != EClientState::GAMEPLAY)
		return false;
	access.requireEmpty();
	const auto * activation = dynamic_cast<const BattleSetActiveStack *>(activePacket);
	const auto registered = client->battleints.find(frames->observer());
	if(!activation || activation->battleID != frames->battle() || activation->stack != frames->activeUnitId()
		|| registered == client->battleints.end() || !sameOwner(registered->second, controller)) return false;
	const auto capabilities = logic->captureSpecialtySerializationContext(connection);
	const auto codec = OwnedBonusCaptureCodec::fromSourceCapabilities(capabilities);
	auto retained = sourceWorld.lock();
	if(!retained || !sameOwner(retained, client->gameStatePtr())) return false;
	std::unique_lock<std::shared_mutex> stateAccess(CGameState::mutex);
	auto pin = BattleRootLifetime::retain(retained);
	if(!pin || !pin->admitted()) return false;
	const auto found = client->battleCallbacks.find(frames->observer());
	if(found == client->battleCallbacks.end() || !found->second) return false;
	const auto callback = found->second->getBattle(frames->battle());
	const auto * native = retained->getBattle(frames->battle());
	if(!callback || !native || callback->getBattle() != native || callback->getPlayerID() != frames->observer()
		|| native->getBattleID() != frames->battle() || native->activeStack != frames->activeUnitId()
		|| native->getActivationSerial() != frames->activationSerial()) return false;
	ownedPreparationAudit::afterClientDecision(*callback, frames->activeUnitId(), frames->activationSerial(), codec.version());
	return pin->admitted(); // Observation ONLY; no candidate capture/import happens here.
#endif
}
