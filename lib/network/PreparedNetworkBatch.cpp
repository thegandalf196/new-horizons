/*
 * PreparedNetworkBatch.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "PreparedNetworkBatchState.h"

PreparedNetworkBatch::State::~State()
{
	// Runs before lock members are destroyed, including constructor failure.
	if(gate->phase.load() == PreparedNetworkGate::Phase::PENDING)
		gate->phase.store(PreparedNetworkGate::Phase::CANCELLED);
}

void PreparedNetworkBatch::State::capture(std::vector<Frame> frames)
{
	deliveries.reserve(frames.size());
	for(auto & [endpoint, bytes] : frames)
	{
		if(!endpoint)
			throw std::runtime_error("Invalid prepared network frame");
		Delivery delivery;
		delivery.tcp = std::dynamic_pointer_cast<NetworkConnection>(endpoint);
		if(delivery.tcp)
		{
			if(bytes.size() > NetworkConnection::messageMaxSize)
				throw std::runtime_error("Prepared TCP frame exceeds receiver size");
			std::vector<std::byte> header(sizeof(uint32_t));
			const uint32_t size = bytes.size();
			std::memcpy(header.data(), &size, sizeof(size));
			delivery.chunks.push_back(std::move(header));
			if(!bytes.empty())
				delivery.chunks.push_back(std::move(bytes));
		}
		else
		{
			delivery.sender = std::dynamic_pointer_cast<InternalConnection>(endpoint);
			if(!delivery.sender)
				throw std::runtime_error("Transport has no prepared publication implementation");
			{
				std::lock_guard lock(delivery.sender->deliveryMutex);
				delivery.receiver = std::dynamic_pointer_cast<InternalConnection>(delivery.sender->otherSideWeak.lock());
			}
			if(!delivery.receiver)
				throw std::runtime_error("Prepared internal peer is absent or unsupported");
			delivery.bytes = std::make_shared<const std::vector<std::byte>>(std::move(bytes));
		}
		deliveries.push_back(std::move(delivery));
	}
}

void PreparedNetworkBatch::State::lockEndpoints()
{
	std::vector<std::mutex *> mutexes;
	for(const auto & delivery : deliveries)
	{
		if(delivery.tcp)
			mutexes.push_back(&delivery.tcp->writeMutex);
		else
		{
			mutexes.push_back(&delivery.sender->deliveryMutex);
			mutexes.push_back(&delivery.receiver->deliveryMutex);
		}
	}
	std::sort(mutexes.begin(), mutexes.end(), std::less<std::mutex *>{});
	mutexes.erase(std::unique(mutexes.begin(), mutexes.end()), mutexes.end());
	locks.reserve(mutexes.size());
	for(auto * mutex : mutexes)
		locks.emplace_back(*mutex);
}

void PreparedNetworkBatch::State::validateEndpoints() const
{
	if(!endpointsReady())
		throw std::runtime_error("Prepared transport binding is closed, stopped or changed");
}

bool PreparedNetworkBatch::State::endpointsReady() const noexcept
{
	for(const auto & delivery : deliveries)
	{
		if(delivery.tcp)
		{
			if(!delivery.tcp->socket->is_open() || delivery.tcp->context.stopped() || delivery.tcp->hasTerminalWriteFailure())
				return false;
		}
		else if(!delivery.sender->connectionActive || !delivery.receiver->connectionActive
			|| delivery.sender->otherSideWeak.lock() != delivery.receiver
			|| delivery.receiver->context.stopped())
			return false;
	}
	return true;
}

PreparedNetworkBatch::PreparedNetworkBatch(std::vector<Frame> frames)
	: state(std::make_unique<State>())
{
	state->capture(std::move(frames));
	state->lockEndpoints();
	state->validateEndpoints();
	state->schedule();
}

PreparedNetworkBatch::~PreparedNetworkBatch() = default;

bool PreparedNetworkBatch::ready() const noexcept
{
	return state->thread == std::this_thread::get_id() && state->scheduled
		&& state->gate->phase.load() == PreparedNetworkGate::Phase::PENDING
		&& state->endpointsReady();
}

void PreparedNetworkBatch::acceptPrepared() noexcept
{
	using Queue = decltype(state->deliveries.front().chunks);
	// list::splice guarantees no exceptions with equal allocators, even when
	// the STL does not declare it noexcept (notably MSVC v142). It transfers
	// prepared nodes without allocating or copying the payloads.
	static_assert(std::allocator_traits<Queue::allocator_type>::is_always_equal::value);
	for(auto & delivery : state->deliveries)
		if(delivery.tcp)
			delivery.tcp->dataToSend.splice(delivery.tcp->dataToSend.end(), delivery.chunks);
	state->gate->phase.store(PreparedNetworkGate::Phase::ACCEPTED);
}

bool PreparedNetworkBatch::accept() noexcept
{
	if(!ready())
		return false;
	acceptPrepared();
	return true;
}
