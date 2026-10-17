/*
 * PreparedNetworkBatch.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "NetworkInterface.h"

class PreparedGameConnectionBatch;
class PreparedBattleWorldPublication;

/// Owns scheduled-but-invisible deliveries for both built-in transports.
/// Contexts/listeners must outlive this owner AND all posted deliveries.
class DLL_LINKAGE PreparedNetworkBatch final : boost::noncopyable
{
	struct State;
	std::unique_ptr<State> state;
	friend class PreparedGameConnectionBatch;
	friend class PreparedBattleWorldPublication;
	void acceptPrepared() noexcept;

public:
	using Frame = std::pair<NetworkConnectionPtr, std::vector<std::byte>>;
	/// Acquires endpoint locks, allocates all queue nodes and posts real handlers.
	/// Throws before acceptance; destruction cancels every scheduled delivery.
	explicit PreparedNetworkBatch(std::vector<Frame> frames);
	~PreparedNetworkBatch();
	bool ready() const noexcept;
	/// Standalone checked publication, not a simulation transaction.
	bool accept() noexcept;
};
