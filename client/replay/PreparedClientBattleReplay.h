/*
 * PreparedClientBattleReplay.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../netlag/NetworkLagReplyState.h"
#include "../../lib/gameState/ReplayPayload.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/bonuses/CBonusSystemNode.h"
#include <shared_mutex>
#include <mutex>
#include <optional>

class CGameState;
class NativeBonusGraphPreparation;
class PreparedReplayAppend;
class PreparedClientBattleReplay;
class PreparedNetworkLagRetirement;
class ClientReceiveAccess;
class ClientPacketReceiveSession;
class ClientReplaySessionReadiness;
namespace battle { class PreparedBattleWorldCommit; class ProjectedBattleWorldArena; }

/// Real client-entry replay STORAGE only. Retain caller/prediction owner and the
/// existing client/interface execution interval until retirement on this thread.
/// No lag rollback, queue consumption, world apply or observers occur here.
std::unique_ptr<PreparedClientBattleReplay> prepareClientBattleReplayEntry(
	std::shared_ptr<CGameState> world, BattleUnitsChanged & packet, const NetworkLagReplyState * predictions);

class PreparedClientBattleReplay final
{
	friend std::unique_ptr<PreparedClientBattleReplay> prepareClientBattleReplayEntry(
		std::shared_ptr<CGameState>, BattleUnitsChanged &, const NetworkLagReplyState *);
	friend class ClientReceiveAccess;
	friend class ClientPacketReceiveSession;
	// Reverse retirement: batch/copies, native interval, state lock, strong world.
	std::shared_ptr<CGameState> world;
	// Original queued payloads retire AFTER native/state locks, before interface unlock.
	std::unique_ptr<PreparedNetworkLagRetirement> queueRetirement;
	ClientReceiveAccess * receiveAccess = nullptr;
	const ClientReplaySessionReadiness * receiveSession = nullptr;
	std::unique_lock<std::shared_mutex> stateAccess;
	std::unique_ptr<NativeBonusGraphPreparation> preparation;
	BattleUnitsChanged & caller;
	BattleUnitsChanged originalInput;
	BattleUnitsChanged writerProjection;
	ReplayPayload snapshot;
	ReplayPayload outerRecord;
	std::optional<BattleReplyInspection> inspection;
	std::unique_ptr<PreparedReplayAppend> batch;
	// Retires world participants/roots BEFORE the remaining client snapshots.
	std::unique_ptr<battle::PreparedBattleWorldCommit> adoptedWorld;
	bool consumed = false;
	PreparedClientBattleReplay(std::shared_ptr<CGameState> world, BattleUnitsChanged & packet);
	void finish(const NetworkLagReplyState * predictions);
public:
	~PreparedClientBattleReplay();
	bool ready() const noexcept;
	/// Nonrouting incoming-battle PROGRAM domain, not a lag policy/filter.
	/// CONSUMED requires queue-only acceptance; rollback requires adventure
	/// roots/observers first. Those programs are NOT implemented here.
	void adoptIncomingBattleWorld();
	const battle::ProjectedBattleWorldArena & projectedWorld() const;
	void prepareWorld(const TNodesVector & mutableNodes, const TCNodesVector & readOnlyNodes);
	bool commitWorldAndReplay() noexcept;
	/// Standalone replay-only acceptance, NOT client world/lag atomicity or routing.
	/// Future common owner must use this ONE batch, not build competing log plans.
	bool commitReplayOnly() noexcept;
	/// Consumed reply only: ONE outer replay record + matched queue unlink.
	/// No battle apply/CAS/observer; actual receive owner retains this child.
	bool commitConsumedReply() noexcept;
	BattleReplyInspection::Result replyAction() const noexcept;
	size_t rollbackCount() const noexcept;
	PreparedClientBattleReplay(const PreparedClientBattleReplay &) = delete;
	PreparedClientBattleReplay & operator=(const PreparedClientBattleReplay &) = delete;
	PreparedClientBattleReplay(PreparedClientBattleReplay &&) = delete;
	PreparedClientBattleReplay & operator=(PreparedClientBattleReplay &&) = delete;
};
