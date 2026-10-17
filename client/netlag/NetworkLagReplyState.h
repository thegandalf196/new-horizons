/*
 * NetworkLagReplyState.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "NetworkLagReplyPrediction.h"
#include <memory>
#include <limits>
#include <vector>
#include <mutex>

struct BattleUnitsChanged;
struct CPackForServer;
struct PackageReceived;
class NetworkLagCompensator;
class NetworkLagReplyState;
class PreparedNetworkLagRetirement;
struct NetworkLagInspectionControl;

/// Actual prediction-derived read-only decision. Owns private rollback copies;
/// does NOT consume prediction state, apply rollback, run observers or grant a lease.
struct BattleReplyInspection
{
	enum class Result { APPLY, CONSUMED, ROLLBACK_THEN_APPLY };
	Result result = Result::APPLY;
	std::vector<std::unique_ptr<CPackForClient>> rollbacks;
	bool stillCurrent() const noexcept;
private:
	friend class NetworkLagReplyState;
	friend class PreparedNetworkLagRetirement;
	Result verifiedResult = Result::APPLY;
	std::weak_ptr<NetworkLagInspectionControl> control;
	std::shared_ptr<const unsigned char> stamp;
};

/// Prediction queue/current-package owner shared by ordinary verification and
/// opt-in battle replay inspection. Same existing client/interface thread interval
/// must cover use/retirement. Inspectors become stale safely when it retires;
/// their owned copies do not extend this queue/world's execution authority.
class NetworkLagReplyState final
{
	friend class NetworkLagCompensator;
	friend class PreparedNetworkLagRetirement;
	std::list<NetworkLagReplyPrediction> predictedReplies;
	static constexpr uint32_t invalidPackageID = std::numeric_limits<uint32_t>::max();
	uint32_t currentPackageID = invalidPackageID;
	std::shared_ptr<NetworkLagInspectionControl> inspectionControl;
public:
	/// Native Compensator binds actual ENGINE interface identity; null keeps
	/// legacy unbound use, but cannot grant prepared queue acceptance.
	explicit NetworkLagReplyState(std::mutex * interfaceMutex = nullptr);
	~NetworkLagReplyState();
	std::unique_ptr<PreparedNetworkLagRetirement> prepareReplyRetirement(
		const BattleReplyInspection & inspection, const std::unique_lock<std::mutex> & interfaceAccess);
	NetworkLagReplyState(const NetworkLagReplyState &) = delete;
	NetworkLagReplyState & operator=(const NetworkLagReplyState &) = delete;
	NetworkLagReplyState(NetworkLagReplyState &&) = delete;
	NetworkLagReplyState & operator=(NetworkLagReplyState &&) = delete;
	void invalidateInspection() noexcept;
	void beginPrediction(const CPackForServer & request);
	void appendPrediction(NetworkLagPredictedPack pack);
	void openReplyPackage(const PackageReceived & packet);
	bool matchesNextReply(const CPackForClient & packet) const;
	BattleReplyInspection inspectBattleReply(const BattleUnitsChanged & packet) const;
};
