/*
 * PreparedGameConnectionBatch.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "../network/NetworkInterface.h"

class GameConnection;
class PreparedBattleWorldPublication;
struct BattleUnitsChanged;

/// Packet-specific encoding/outbox preparation; never applies simulation state.
/// Call on the publisher's running network context, with its recipient vector
/// excluded from mutation until this owner is retired on that same thread.
class DLL_LINKAGE PreparedGameConnectionBatch final : boost::noncopyable
{
	struct State;
	std::unique_ptr<State> state;
	friend class PreparedBattleWorldPublication;
	void acceptPrepared() noexcept;

public:
	PreparedGameConnectionBatch(NetworkContext & publisher,
		const std::vector<std::shared_ptr<GameConnection>> & recipients,
		const BattleUnitsChanged & packet);
	~PreparedGameConnectionBatch();
	/// Exact post-writer packet for subsequent world preparation. The caller's
	/// input is unchanged; zero recipients means no writer mutation.
	const BattleUnitsChanged & packet() const noexcept;
	bool ready() const noexcept;
	/// Standalone publication only. World/graph/replay atomicity is NOT implied.
	bool accept() noexcept;
};
