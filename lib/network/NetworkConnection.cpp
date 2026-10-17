/*
 * NetworkConnection.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NetworkConnection.h"
#include "NetworkWriteAllocation.h"
#include <boost/asio/post.hpp>
#ifdef NH_PERF_EXPERIMENTS
#include "../networkPacks/PacksForServer.h"
#endif

NetworkConnection::NetworkConnection(INetworkConnectionListener & listener, const std::shared_ptr<NetworkSocket> & socket, NetworkContext & context)
	: context(context)
	, socket(socket)
	, timer(std::make_shared<NetworkTimer>(context))
	, listener(listener)
{
	socket->set_option(boost::asio::ip::tcp::no_delay(true));

	// iOS throws exception on attempt to set buffer size
	constexpr auto bufferSize = 4 * 1024 * 1024;

	try
	{
		socket->set_option(boost::asio::socket_base::send_buffer_size{bufferSize});
	}
	catch(const boost::system::system_error & e)
	{
		logNetwork->error("error setting 'send buffer size' socket option: %s", e.what());
	}

	try
	{
		socket->set_option(boost::asio::socket_base::receive_buffer_size{bufferSize});
	}
	catch(const boost::system::system_error & e)
	{
		logNetwork->error("error setting 'receive buffer size' socket option: %s", e.what());
	}
}

NetworkConnection::NetworkConnection(INetworkConnectionListener & listener,
	const std::shared_ptr<NetworkSocket> & socket, NetworkContext & context,
	std::shared_ptr<NetworkWriteAllocationControl> allocationControl)
	: NetworkConnection(listener, socket, context)
{
	if(!allocationControl)
		throw std::invalid_argument("Missing network write allocation resource");
	writeAllocationControl = std::move(allocationControl);
}

void NetworkConnection::start()
{
	heartbeat();
	startReceiving();
}

void NetworkConnection::startReceiving()
{
	boost::asio::async_read(*socket,
							readBuffer,
							boost::asio::transfer_exactly(messageHeaderSize),
							[self = shared_from_this()](const auto & ec, const auto & endpoint) { self->onHeaderReceived(ec); });
}

void NetworkConnection::heartbeat()
{
	if(writeTerminal.load())
		return;

	constexpr auto heartbeatInterval = std::chrono::seconds(10);

	timer->expires_after(heartbeatInterval);
	timer->async_wait( [self = weak_from_this()](const auto & ec)
	{
		if (ec)
			return;

		auto locked = self.lock();
		if (!locked)
			return;

		locked->sendPacket({});
		locked->heartbeat();
	});
}

void NetworkConnection::onHeaderReceived(const boost::system::error_code & ecHeader)
{
	if(writeTerminal.load())
		return; // Cancellation of a terminal writer is already reported.

	if (ecHeader)
	{
		onError(ecHeader.message());
		return;
	}

	if (readBuffer.size() < messageHeaderSize)
		throw std::runtime_error("Failed to read header!");

	uint32_t messageSize;
	readBuffer.sgetn(reinterpret_cast<char *>(&messageSize), sizeof(messageSize));

	if (messageSize > messageMaxSize)
	{
		onError("Invalid packet size!");
		return;
	}

	if (messageSize == 0)
	{
		//heartbeat package with no payload - wait for next packet
		startReceiving();
		return;
	}

	boost::asio::async_read(*socket,
							readBuffer,
							boost::asio::transfer_exactly(messageSize),
							[self = shared_from_this(), messageSize](const auto & ecPayload, const auto & endpoint) { self->onPacketReceived(ecPayload, messageSize); });
}

void NetworkConnection::onPacketReceived(const boost::system::error_code & ec, uint32_t expectedPacketSize)
{
	if(writeTerminal.load())
		return; // Cancellation of a terminal writer is already reported.

	if (ec)
	{
		onError(ec.message());
		return;
	}

	if (readBuffer.size() < expectedPacketSize)
	{
		// FIXME: figure out what causes this. This should not be possible without error set
		std::string errorMessage = "Failed to read packet! " + std::to_string(readBuffer.size()) + " bytes read, but " + std::to_string(expectedPacketSize) + " bytes expected!";
		onError(errorMessage);
	}

	std::vector<std::byte> message(expectedPacketSize);
	readBuffer.sgetn(reinterpret_cast<char *>(message.data()), expectedPacketSize);
	listener.onPacketReceived(shared_from_this(), message);

	startReceiving();
}

void NetworkConnection::setAsyncWritesEnabled(bool on)
{
	std::lock_guard lock(writeMutex);
	asyncWritesEnabled = on;
}

void NetworkConnection::sendPacket(const std::vector<std::byte> & message)
{
	std::lock_guard lock(writeMutex);
	if(writeTerminal.load())
		return;
	std::vector<std::byte> headerVector(sizeof(uint32_t));
	uint32_t messageSize = message.size();
	std::memcpy(headerVector.data(), &messageSize, sizeof(uint32_t));

	// At the moment, vcmilobby *requires* async writes in order to handle multiple connections with different speeds and at optimal performance
	// However server (and potentially - client) can not handle this mode and may shutdown either socket or entire asio service too early, before all writes are performed
	if (asyncWritesEnabled || writeInProgress)
	{

		bool messageQueueEmpty = dataToSend.empty();
		std::list<std::vector<std::byte>> chunks;
		chunks.push_back(std::move(headerVector));
		if (!message.empty())
			chunks.push_back(message);
		dataToSend.splice(dataToSend.end(), chunks);

		if (messageQueueEmpty)
			doSendData();
		//else - data sending loop is still active and still sending previous messages
	}
	else
	{
		// Keep ordinary sync send blocking: drain accepted prepared frames first.
		const auto error = drainSynchronousWrites();
		if(error)
		{
			recordWriteFailure(NetworkWriteFailureInfo::Origin::SYNCHRONOUS_DRAIN, {}, error);
			return; // The already-posted prepared starter reports outside writer locks.
		}
		boost::system::error_code ec;
		boost::asio::write(*socket, boost::asio::buffer(headerVector), ec );
		if (!message.empty())
			boost::asio::write(*socket, boost::asio::buffer(message), ec );
	}
}

void NetworkConnection::doSendData()
{
	if(dataToSend.empty() || writeOffset >= dataToSend.front().size())
		throw std::logic_error("Invalid queued network write offset");
	const auto & front = dataToSend.front();
	const auto limit = writeAllocationControl ? writeAllocationControl->nativeWriteLimit() : front.size();
	const auto size = std::min(front.size() - writeOffset, limit);
	auto handler = [self = shared_from_this()](const auto & error, size_t transferred)
	{
		self->onDataSent(error, transferred);
	};
	writeInProgress = true;
	try
	{
		const auto bytes = boost::asio::buffer(front.data() + writeOffset, size);
		if(writeAllocationControl)
			socket->async_write_some(bytes, ControlledNetworkWriteHandler{std::move(handler), writeAllocationControl});
		else
			socket->async_write_some(bytes, std::move(handler));
	}
	catch(...)
	{
		writeInProgress = false;
		throw;
	}
}

void NetworkConnection::onDataSent(const boost::system::error_code & ec, size_t transferred)
{
	bool failed = false;
	{
		std::lock_guard lock(writeMutex);
		if(writeTerminal.load())
			return; // Retain buffers until canceled native operations have retired.
		writeInProgress = false;
		if(ec)
		{
			if(!dataToSend.empty() && writeOffset <= dataToSend.front().size()
				&& transferred <= dataToSend.front().size() - writeOffset)
				writeOffset += transferred; // Retain reported progress; terminal state never retries.
			recordWriteFailure(NetworkWriteFailureInfo::Origin::COMPLETION, {}, ec);
			failed = true;
		}
		else if(dataToSend.empty() || transferred == 0
			|| transferred > dataToSend.front().size() - writeOffset)
		{
			recordWriteFailure(NetworkWriteFailureInfo::Origin::INVALID_COMPLETION, {},
				boost::system::errc::make_error_code(boost::system::errc::io_error));
			failed = true;
		}
		else
		{
			writeOffset += transferred;
			if(writeOffset == dataToSend.front().size())
			{
				dataToSend.pop_front();
				writeOffset = 0;
			}
			if(!dataToSend.empty())
				failed = !trySendData(); // Includes partial writes within one chunk.
		}
	}
	if(failed)
		finishWriteFailure();
}

void NetworkConnection::onError(const std::string & message)
{
	listener.onDisconnected(shared_from_this(), message);
	close();
}

void NetworkConnection::close()
{
	std::lock_guard lock(writeMutex);
	boost::system::error_code ec;
	socket->close(ec);
#if BOOST_VERSION >= 108700
	timer->cancel();
#else
	timer->cancel(ec);
#endif

	//NOTE: ignoring error code, intended
}

InternalConnection::InternalConnection(INetworkConnectionListener & listener, NetworkContext & context
#ifdef NH_PERF_EXPERIMENTS
	, const std::shared_ptr<ExperimentalSetFormationLease> & receiver
#endif
)
	: deliveryStrand(context)
	, context(context)
	, listener(listener)
#ifdef NH_PERF_EXPERIMENTS
	, experimentalCapable(receiver != nullptr)
	, experimentalReceiver(receiver)
#endif
{
}

#ifdef NH_PERF_EXPERIMENTS
ExperimentalSetFormationResult queueExperimentalSetFormation(const NetworkConnectionPtr & connection, const SetFormation & pack)
{
	if(!connection)
		return ExperimentalSetFormationResult::CLOSED;
	auto internal = std::dynamic_pointer_cast<InternalConnection>(connection);
	if(!internal)
		return ExperimentalSetFormationResult::NOT_ELIGIBLE;
	return internal->queueSetFormation(pack);
}

ExperimentalSetFormationResult InternalConnection::queueSetFormation(const SetFormation & pack)
{
	if(!experimentalOpen.load())
		return ExperimentalSetFormationResult::CLOSED;
	// This separate weak object is immutable after binding. Do not read the
	// ordinary otherSideWeak here: disconnect/close can reset it concurrently.
	auto other = experimentalPeer.lock();
	if(!other)
		return ExperimentalSetFormationResult::CLOSED;
	auto peer = std::dynamic_pointer_cast<InternalConnection>(other);
	if(!peer)
		return ExperimentalSetFormationResult::NOT_ELIGIBLE;
	if(!peer->experimentalOpen.load())
		return ExperimentalSetFormationResult::CLOSED;
	if(!peer->experimentalCapable)
		return ExperimentalSetFormationResult::NOT_ELIGIBLE;
	if(peer->experimentalReceiver.expired())
		return ExperimentalSetFormationResult::CLOSED;

	// Value copy of one flat request, not a callable or arbitrary typed packet.
	// Once post succeeds there is no fallback, even if shutdown cancels delivery.
	boost::asio::post(peer->deliveryStrand, [peer, request = pack]() mutable
	{
		{
			std::lock_guard lock(peer->deliveryMutex);
			if(!peer->connectionActive || !peer->experimentalOpen.load())
				return;
		}
		auto receiver = peer->experimentalReceiver.lock();
		if(receiver)
			receiver->receiver->onExperimentalSetFormation(peer, request);
	});
	return ExperimentalSetFormationResult::QUEUED;
}
#endif
