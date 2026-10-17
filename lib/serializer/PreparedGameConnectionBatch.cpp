/*
 * PreparedGameConnectionBatch.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "PreparedGameConnectionState.h"
#include "../network/NetworkConnection.h"
#include "../networkPacks/PacksForClientBattle.h"

int PreparedGameConnectionBatch::State::Writer::write(const std::byte * data, unsigned size)
{
	// TCP uses its existing receiver limit; internal connections retain their
	// existing vector allocation domain, not a newly invented wire-size ceiling.
	if(size > maximum - bytes.size())
		throw std::runtime_error("Prepared packet exceeds transport frame size");
	bytes.insert(bytes.end(), data, data + size);
	return size;
}

PreparedGameConnectionBatch::State::State(NetworkContext & publisher,
	const std::vector<std::shared_ptr<GameConnection>> & recipients)
	: publisher(publisher), recipients(recipients), data(recipients.data()), capacity(recipients.capacity()), occurrences(recipients)
{
	if(!publisher.get_executor().running_in_this_thread())
		throw std::logic_error("Prepared publication requires its running publisher context");
}

void PreparedGameConnectionBatch::State::lockConnections()
{
	std::vector<GameConnection *> unique;
	for(const auto & connection : occurrences)
	{
		if(!connection)
			throw std::runtime_error("Null prepared recipient");
		unique.push_back(connection.get());
	}
	std::sort(unique.begin(), unique.end(), std::less<GameConnection *>{});
	unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
	locks.reserve(unique.size());
	encoders.reserve(unique.size());
	for(auto * connection : unique)
		locks.emplace_back(connection->writeMutex);
	for(auto * connection : unique)
	{
		auto encoder = std::make_unique<Encoder>();
		encoder->connection = *std::find_if(occurrences.begin(), occurrences.end(),
			[connection](const auto & entry) { return entry.get() == connection; });
		encoder->id = connection->connectionID;
		encoder->uuid = connection->uuid;
		encoder->endpoint = connection->networkConnection.lock();
		if(!encoder->endpoint)
			throw std::runtime_error("Prepared recipient is closed");
		if(std::dynamic_pointer_cast<NetworkConnection>(encoder->endpoint))
			encoder->writer.maximum = NetworkConnection::messageMaxSize;
		else if(!std::dynamic_pointer_cast<InternalConnection>(encoder->endpoint))
			throw std::runtime_error("Transport has no prepared publication implementation");
		if(!connection->serializer->savedPointers.empty())
			throw std::runtime_error("Prepared recipient has unfinished pointer serialization");
		encoder->staged = std::make_unique<BinarySerializer>(*connection->serializer);
		encoder->staged->writer = &encoder->writer;
		encoders.push_back(std::move(encoder));
	}
}

void PreparedGameConnectionBatch::State::encode(const BattleUnitsChanged & packet)
{
	this->packet = std::make_unique<BattleUnitsChanged>(packet);
	std::vector<PreparedNetworkBatch::Frame> frames;
	frames.reserve(occurrences.size());
	for(const auto & connection : occurrences)
	{
		auto found = std::find_if(encoders.begin(), encoders.end(),
			[&connection](const auto & encoder) { return encoder->connection == connection; });
		auto & encoder = **found;
		// applyPack gives every writer the SAME packet, then applies that final
		// writer projection. Preserve this ordered mutation on our owned copy;
		// the future world owner must consume packet(), not the caller's input.
		const CPack * base = this->packet.get();
		*encoder.staged & base;
		frames.emplace_back(encoder.endpoint, std::move(encoder.writer.bytes));
		encoder.writer.bytes.clear();
		encoder.staged->clear(); // Strings deliberately persist, exactly as sendPack.
	}
	outbox = std::make_unique<PreparedNetworkBatch>(std::move(frames));
}

PreparedGameConnectionBatch::PreparedGameConnectionBatch(NetworkContext & publisher,
	const std::vector<std::shared_ptr<GameConnection>> & recipients, const BattleUnitsChanged & packet)
	: state(std::make_unique<State>(publisher, recipients))
{
	state->lockConnections();
	state->encode(packet);
}

PreparedGameConnectionBatch::~PreparedGameConnectionBatch() = default;

const BattleUnitsChanged & PreparedGameConnectionBatch::packet() const noexcept
{
	return *state->packet;
}

bool PreparedGameConnectionBatch::ready() const noexcept
{
	if(state->accepted || state->thread != std::this_thread::get_id()
		|| !state->publisher.get_executor().running_in_this_thread() || state->publisher.stopped()
		|| state->recipients.data() != state->data || state->recipients.capacity() != state->capacity
		|| state->recipients != state->occurrences || !state->outbox->ready())
		return false;
	for(const auto & encoder : state->encoders)
		if(encoder->connection->connectionID != encoder->id || encoder->connection->uuid != encoder->uuid
			|| encoder->connection->networkConnection.lock() != encoder->endpoint)
			return false;
	return true;
}

void PreparedGameConnectionBatch::acceptPrepared() noexcept
{
	static_assert(noexcept(state->encoders.front()->connection->serializer->savedStrings.swap(
		state->encoders.front()->staged->savedStrings)));
	static_assert(noexcept(state->encoders.front()->connection->serializer->savedPointers.swap(
		state->encoders.front()->staged->savedPointers)));
	for(auto & encoder : state->encoders)
	{
		auto & live = *encoder->connection->serializer;
		live.savedStrings.swap(encoder->staged->savedStrings);
		live.savedPointers.swap(encoder->staged->savedPointers);
	}
	state->outbox->acceptPrepared();
	state->accepted = true;
}

bool PreparedGameConnectionBatch::accept() noexcept
{
	if(!ready())
		return false;
	acceptPrepared();
	return true;
}
