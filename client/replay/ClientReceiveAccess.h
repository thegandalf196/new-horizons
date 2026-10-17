/*
 * ClientReceiveAccess.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <memory>
#include <mutex>
#include <thread>

class CGameState;
class NetworkLagReplyState;
class NetworkLagCompensator;
class PreparedClientBattleReplay;
class ClientPacketReceiveSession;
struct BattleUnitsChanged;

/// Owns an ALREADY acquired lock, never locks/unlocks it for another interval.
/// Nonmovable; construct/use/retire on the locking thread. The native receive
/// session below supplies the real ENGINE mutex and session identities. This
/// lower storage owner alone does NOT authenticate an Engine/client/session.
/// No child can escape ownership: references expire with this receive interval.
class ClientReceiveAccess final
{
	friend class ClientPacketReceiveSession;
	std::unique_lock<std::mutex> interfaceAccess;
	std::mutex * const expectedMutex;
	const std::thread::id thread = std::this_thread::get_id();
	std::unique_ptr<PreparedClientBattleReplay> child;
	PreparedClientBattleReplay & prepareConsumed(std::shared_ptr<CGameState> world,
		BattleUnitsChanged & packet, NetworkLagCompensator & predictions, ClientPacketReceiveSession & session);
	void requireEmpty() const;
public:
	/// A null identity preserves an ENGINE-absent ordinary receive, but cannot
	/// grant prepared acceptance. Moving the guard leaves its source unowned.
	ClientReceiveAccess(std::unique_lock<std::mutex> && access, std::mutex * exactMutex);
	~ClientReceiveAccess();
	bool heldOnThisThread() const noexcept;
	bool owns(const PreparedClientBattleReplay & entry) const noexcept;
	/// Concrete lower queue/replay composition; actual session uses private overload.
	PreparedClientBattleReplay & prepareConsumed(std::shared_ptr<CGameState> world,
		BattleUnitsChanged & packet, NetworkLagReplyState & predictions);
	ClientReceiveAccess(const ClientReceiveAccess &) = delete;
	ClientReceiveAccess & operator=(const ClientReceiveAccess &) = delete;
	ClientReceiveAccess(ClientReceiveAccess &&) = delete;
	ClientReceiveAccess & operator=(ClientReceiveAccess &&) = delete;
};
