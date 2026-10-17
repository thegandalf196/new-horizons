/*
 * PreparedNetworkLagRetirement.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "NetworkLagReplyState.h"
#include <thread>

class PreparedClientBattleReplay;

/// Queue storage participant ONLY: no world rollback, replay acceptance or UI.
/// Borrow the REAL preacquired interface unique_lock on its originating thread;
/// it must outlive this object, without move/unlock/relock until acceptance or
/// abandonment. Checks do not prove historical lock continuity or thread ownership.
/// Source owner must survive prepare/use; checked refusal handles later retirement,
/// not concurrent unsynchronized alias use. Retire owned payloads only AFTER common
/// native/root/state participants release, while client/session remain retained.
class PreparedNetworkLagRetirement final
{
	friend class NetworkLagReplyState;
	friend class PreparedClientBattleReplay; // future common caller, no routing here
	const std::unique_lock<std::mutex> & interfaceAccess;
	const std::thread::id thread = std::this_thread::get_id();
	std::shared_ptr<NetworkLagInspectionControl> control;
	std::shared_ptr<const unsigned char> expectedStamp;
	NetworkLagReplyPrediction * prediction = nullptr;
	NetworkLagPredictedPack * first = nullptr;
	size_t writtenCount = 0;
	uint32_t requestID = 0;
	BattleReplyInspection::Result action;
	std::vector<const CPackForClient *> rollbackInputs;
	std::list<NetworkLagReplyPrediction> retiredPredictions;
	std::list<NetworkLagPredictedPack> retiredWritten;
	std::shared_ptr<const unsigned char> retiredStamp;
	bool consumed = false;
	PreparedNetworkLagRetirement(NetworkLagReplyState & source,
		const BattleReplyInspection & inspection, const std::unique_lock<std::mutex> & access);
	/// Private preowned tail; readiness/exclusion must precede any common CAS.
	void exchangePreparedStorage() noexcept;
public:
	~PreparedNetworkLagRetirement();
	bool ready() const noexcept;
	/// Checked standalone QUEUE acceptance, not a world/replay transaction.
	bool commit() noexcept;
	/// Exact original CPack identities in native rollback order. Before commit,
	/// these remain borrowed: never use outside the retained source interval.
	const std::vector<const CPackForClient *> & rollbackPackets() const;
	PreparedNetworkLagRetirement(const PreparedNetworkLagRetirement &) = delete;
	PreparedNetworkLagRetirement & operator=(const PreparedNetworkLagRetirement &) = delete;
	PreparedNetworkLagRetirement(PreparedNetworkLagRetirement &&) = delete;
	PreparedNetworkLagRetirement & operator=(PreparedNetworkLagRetirement &&) = delete;
};
