/*
 * PreparedBattleWorldPublication.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../lib/networkPacks/BattleChanges.h"
#include "../lib/bonuses/CBonusSystemNode.h"
#include <memory>

class CVCMIServer;
class CGameHandler;
class CGameState;
class PreparedGameConnectionBatch;
struct BattleUnitsChanged;
namespace battle { class PreparedBattleWorldCommit; class ProjectedBattleWorldArena; }

/// Actual server world + recipient storage owner, not yet ordinary dispatch.
/// Created only by CVCMIServer on its running publisher context. The server,
/// event loop/listeners and caller-original packet must outlive this owner.
/// Retire on the same thread. No callbacks, joins or GUI work while it is held.
/// Retains an exact original input snapshot: same-thread caller changes refuse
/// before CAS. Concurrent unsynchronized packet access remains a data race.
/// Real native mutable/read-only provenance and external source/config/alias
/// ownership remain required; this class does not derive or fabricate them.
class PreparedBattleWorldPublication final
{
	friend class CVCMIServer;
	CVCMIServer & server;
	std::shared_ptr<CGameHandler> authority;
	CGameState * originalWorld;
	BattleUnitsChanged & caller;
	std::unique_ptr<const BattleUnitsChanged> callerInput;
	// Reverse retirement: packet storage, world/graph/state, then recipients.
	// Canceled/accepted posted handlers retain their independent delivery state.
	std::unique_ptr<PreparedGameConnectionBatch> recipients;
	std::unique_ptr<battle::PreparedBattleWorldCommit> world;
	std::vector<UnitChanges> callerProjection;
	bool consumed = false;
	PreparedBattleWorldPublication(CVCMIServer & server, BattleUnitsChanged & packet);
public:
	~PreparedBattleWorldPublication();
	const battle::ProjectedBattleWorldArena & projected() const;
	void prepare(const TNodesVector & mutableNodes, const TCNodesVector & readOnlyNodes);
	bool ready() const noexcept;
	/// Recipient freshness precedes world CAS. Success runs existing world tails,
	/// preserves the caller's final writer projection, then no-fail acceptance.
	/// No transport I/O starts synchronously here; R1 retains accepted failures.
	bool commit() noexcept;
	PreparedBattleWorldPublication(const PreparedBattleWorldPublication &) = delete;
	PreparedBattleWorldPublication & operator=(const PreparedBattleWorldPublication &) = delete;
	PreparedBattleWorldPublication(PreparedBattleWorldPublication &&) = delete;
	PreparedBattleWorldPublication & operator=(PreparedBattleWorldPublication &&) = delete;
};
