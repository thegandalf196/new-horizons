/*
 * ClientPacketReceiveSession.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "ClientReceiveAccess.h"
#include "ClientReplaySessionReadiness.h"
#include "../../lib/constants/EntityIdentifiers.h"

class CServerHandler;
class CClient;
class GameEngine;
class GameConnection;
class INetworkConnection;
struct CPackForClient;
struct OwnedRecipientCaptureMetadata;

/// Native receive-stack owner, created ONLY at onPacketReceived's real lock site.
/// Captured identities are not an arbitrary lifetime lease: actual ENGINE and
/// handler destruction still require existing stop/join-before-destruction order.
/// This object must never cross threads or survive that synchronous invocation.
/// Its child retires before interface unlock; no observer/GUI wait while child lives.
class ClientPacketReceiveSession final : private ClientReplaySessionReadiness
{
	friend class CServerHandler;
	friend class ClientReceiveAccess;
	CServerHandler & handler;
	GameEngine * const engine;
	std::shared_ptr<INetworkConnection> connection;
	std::shared_ptr<GameConnection> logic;
	CClient * const client;
	NetworkLagCompensator * const predictions;
	CGameState * const world;
	std::weak_ptr<CGameState> sourceWorld; // Original ownership identity, not a long world lease.
	CPackForClient * activePacket = nullptr;
	// LAST: child/native/state retirement then interface unlock BEFORE identities.
	ClientReceiveAccess access;
	ClientPacketReceiveSession(CServerHandler & handler, std::unique_lock<std::mutex> && heldAccess,
		GameEngine * engine, const std::shared_ptr<INetworkConnection> & connection);
public:
	~ClientPacketReceiveSession() override;
	void visitForClient(CPackForClient & packet);
	/// UNUSED partial capture: owned observable metadata only, never a query/dispatch permit.
	OwnedRecipientCaptureMetadata captureRecipientMetadata(PlayerColor observer, BattleID battle, uint32_t unit);
	bool ready(const BattleUnitsChanged & packet) const noexcept override;
	/// Opt-in only; normal dispatch below is unchanged until all lag programs exist.
	/// Borrowed result stays owned by this receive stack, not an escaping unique_ptr.
	PreparedClientBattleReplay & prepareConsumedReply(BattleUnitsChanged & packet);
	ClientPacketReceiveSession(const ClientPacketReceiveSession &) = delete;
	ClientPacketReceiveSession & operator=(const ClientPacketReceiveSession &) = delete;
	ClientPacketReceiveSession(ClientPacketReceiveSession &&) = delete;
	ClientPacketReceiveSession & operator=(ClientPacketReceiveSession &&) = delete;
};
