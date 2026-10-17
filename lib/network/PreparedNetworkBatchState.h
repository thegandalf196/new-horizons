/*
 * PreparedNetworkBatchState.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "PreparedNetworkBatch.h"
#include "NetworkConnection.h"
#include <atomic>
#include <thread>

struct PreparedNetworkGate
{
	enum class Phase { PENDING, ACCEPTED, CANCELLED };
	std::atomic<Phase> phase{Phase::PENDING};
};

struct PreparedNetworkBatch::State
{
	struct Delivery
	{
		std::shared_ptr<NetworkConnection> tcp;
		std::shared_ptr<InternalConnection> sender;
		std::shared_ptr<InternalConnection> receiver;
		std::shared_ptr<const std::vector<std::byte>> bytes;
		std::list<std::vector<std::byte>> chunks;
	};
	std::shared_ptr<PreparedNetworkGate> gate = std::make_shared<PreparedNetworkGate>();
	std::vector<Delivery> deliveries;
	std::vector<std::unique_lock<std::mutex>> locks;
	std::thread::id thread = std::this_thread::get_id();
	bool scheduled = false;
	~State();
	void capture(std::vector<Frame> frames);
	void lockEndpoints();
	void validateEndpoints() const;
	bool endpointsReady() const noexcept;
	void schedule();
};
