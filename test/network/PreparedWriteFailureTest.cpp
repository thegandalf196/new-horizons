/*
 * PreparedWriteFailureTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/network/NetworkConnection.h"
#include "../../lib/network/NetworkWriteAllocation.h"
#include "../../lib/network/PreparedNetworkBatch.h"
#include <chrono>
#include <thread>

namespace
{
static_assert(noexcept(std::declval<PreparedNetworkBatch &>().accept()),
	"Prepared publication must retain its non-throwing acceptance boundary");

class FailureRecorder final : public INetworkConnectionListener
{
public:
	size_t disconnected = 0;
	bool throwDuringNotification = false;
	bool observedTerminalOutsideLock = false;
	void onPacketReceived(const NetworkConnectionPtr &, const std::vector<std::byte> &) override {}
	void onDisconnected(const NetworkConnectionPtr & connection, const std::string &) override
	{
		++disconnected;
		auto tcp = std::dynamic_pointer_cast<NetworkConnection>(connection);
		// This reacquires writeMutex: a retained endpoint lock would deadlock.
		observedTerminalOutsideLock = tcp && tcp->writeStatus().terminal;
		if(throwDuringNotification)
			throw std::bad_alloc();
	}
};

bool isAllocationFailure(const std::exception_ptr & cause)
{
	if(!cause)
		return false;
	try { std::rethrow_exception(cause); }
	catch(const std::bad_alloc &) { return true; }
	catch(...) { return false; }
}

class PreparedWriteFailureTest : public ::testing::Test
{
protected:
	NetworkContext context;
	FailureRecorder listener;
	boost::asio::ip::tcp::acceptor acceptor{context, {boost::asio::ip::address_v4::loopback(), 0}};
	NetworkSocket peer{context};
	std::shared_ptr<NetworkSocket> socket = std::make_shared<NetworkSocket>(context);
	std::shared_ptr<NetworkWriteAllocationControl> allocation;
	std::shared_ptr<NetworkConnection> endpoint;
	const std::vector<std::byte> payload = std::vector<std::byte>(64, std::byte{0x53});

	void connect(size_t limit = std::numeric_limits<size_t>::max())
	{
		peer.connect(acceptor.local_endpoint());
		acceptor.accept(*socket);
		allocation = std::make_shared<NetworkWriteAllocationControl>(limit);
		endpoint = std::make_shared<NetworkConnection>(listener, socket, context, allocation);
		endpoint->setAsyncWritesEnabled(true);
	}

	void acceptFrame()
	{
		PreparedNetworkBatch batch({{endpoint, payload}});
		ASSERT_TRUE(batch.ready());
		ASSERT_TRUE(batch.accept());
		EXPECT_FALSE(batch.accept());
	}

	void expectTerminal()
	{
		const auto status = endpoint->writeStatus();
		EXPECT_TRUE(status.terminal);
		EXPECT_EQ(status.failure.origin, NetworkWriteFailureInfo::Origin::INITIATION);
		EXPECT_TRUE(isAllocationFailure(status.failure.cause));
		EXPECT_TRUE(status.failure.notificationAttempted);
		EXPECT_FALSE(status.failure.socketStillOpen);
		EXPECT_FALSE(status.failure.cleanupFailure);
		EXPECT_FALSE(socket->is_open());
		EXPECT_EQ(listener.disconnected, 1);
		EXPECT_TRUE(listener.observedTerminalOutsideLock);
	}

	bool reachPayloadPartial()
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while(std::chrono::steady_clock::now() < deadline)
		{
			context.poll_one();
			const auto status = endpoint->writeStatus();
			if(status.queuedChunks == 1 && status.frontOffset >= 1)
				return true;
			if(status.terminal || status.queuedChunks == 0)
				return false;
			std::this_thread::yield();
		}
		return false;
	}
};

TEST_F(PreparedWriteFailureTest, FirstNativeAllocationFailureIsTerminalAndContained)
{
	connect();
	allocation->refuse();
	acceptFrame();
	EXPECT_NO_THROW(context.run());
	expectTerminal();
	EXPECT_GE(allocation->attempts(), 1);
	EXPECT_EQ(endpoint->writeStatus().queuedChunks, 2);
	EXPECT_EQ(endpoint->writeStatus().frontOffset, 0);
}

TEST_F(PreparedWriteFailureTest, FirstSuccessfulPartialHeaderThenInitiationFailureIsContained)
{
	connect(1);
	acceptFrame();
	ASSERT_EQ(context.poll_one(), 1); // Posted starter initiates a REAL one-byte write.
	ASSERT_TRUE(endpoint->writeStatus().inFlight);
	allocation->refuse();
	EXPECT_NO_THROW(context.run());
	expectTerminal();
	EXPECT_EQ(endpoint->writeStatus().frontOffset, 1);
	EXPECT_EQ(endpoint->writeStatus().queuedChunks, 2);
}

TEST_F(PreparedWriteFailureTest, PartialPayloadThenLaterInitiationFailureRetainsItsOffset)
{
	connect(1);
	acceptFrame();
	ASSERT_TRUE(reachPayloadPartial());
	const auto before = endpoint->writeStatus();
	ASSERT_TRUE(before.inFlight);
	allocation->refuse();
	EXPECT_NO_THROW(context.run());
	expectTerminal();
	const auto after = endpoint->writeStatus();
	EXPECT_EQ(after.queuedChunks, 1);
	EXPECT_GT(after.frontOffset, before.frontOffset);
	EXPECT_LT(after.frontOffset, payload.size());
}

TEST_F(PreparedWriteFailureTest, PersistentRefusalAndThrowingObserverCannotEscapeOrRestart)
{
	connect();
	listener.throwDuringNotification = true;
	allocation->refuse();
	acceptFrame();
	EXPECT_NO_THROW(context.run());
	expectTerminal();
	EXPECT_TRUE(isAllocationFailure(endpoint->writeStatus().failure.observationFailure));
	const auto attempts = allocation->attempts();
	context.restart();
	EXPECT_NO_THROW(endpoint->sendPacket(payload));
	EXPECT_NO_THROW(context.run());
	EXPECT_EQ(allocation->attempts(), attempts);
	EXPECT_EQ(listener.disconnected, 1);
}

TEST_F(PreparedWriteFailureTest, AllowingAllocationLaterDoesNotRetryAnIncompleteAcceptedFrame)
{
	connect(1);
	acceptFrame();
	ASSERT_EQ(context.poll_one(), 1);
	allocation->refuse();
	EXPECT_NO_THROW(context.run());
	expectTerminal();
	const auto before = endpoint->writeStatus();
	const auto attempts = allocation->attempts();
	allocation->allow();
	context.restart();
	endpoint->sendPacket(payload);
	EXPECT_THROW((PreparedNetworkBatch({{endpoint, payload}})), std::runtime_error);
	EXPECT_NO_THROW(context.run());
	EXPECT_EQ(endpoint->writeStatus().frontOffset, before.frontOffset);
	EXPECT_EQ(endpoint->writeStatus().queuedChunks, before.queuedChunks);
	EXPECT_EQ(allocation->attempts(), attempts);
	EXPECT_EQ(listener.disconnected, 1);
}

TEST_F(PreparedWriteFailureTest, CancellationNeverAttemptsNativeWriteAllocation)
{
	connect();
	allocation->refuse();
	{ PreparedNetworkBatch cancelled({{endpoint, payload}}); }
	EXPECT_NO_THROW(context.run());
	EXPECT_EQ(allocation->attempts(), 0);
	EXPECT_EQ(listener.disconnected, 0);
	EXPECT_FALSE(endpoint->writeStatus().terminal);
	EXPECT_TRUE(endpoint->writeStatus().queuedChunks == 0);
}

TEST_F(PreparedWriteFailureTest, CancelledReadAndHeartbeatDoNotNotifyAgainAfterWriteFailure)
{
	connect();
	endpoint->start(); // Real read and timer cancellation callbacks will run too.
	allocation->refuse();
	acceptFrame();
	EXPECT_NO_THROW(context.run());
	expectTerminal();
	EXPECT_FALSE(endpoint->writeStatus().failure.observationFailure);
	context.restart();
	EXPECT_NO_THROW(context.poll());
	EXPECT_EQ(listener.disconnected, 1);
}

TEST_F(PreparedWriteFailureTest, PartialWritesPreserveCompleteNativeHeaderAndPayloadOrder)
{
	connect(1);
	acceptFrame();
	acceptFrame();
	EXPECT_NO_THROW(context.run());
	const auto status = endpoint->writeStatus();
	EXPECT_FALSE(status.terminal);
	EXPECT_FALSE(status.inFlight);
	EXPECT_EQ(status.frontOffset, 0);
	EXPECT_EQ(status.queuedChunks, 0);
	EXPECT_EQ(listener.disconnected, 0);
	const uint32_t size = payload.size();
	std::vector<std::byte> expected(sizeof(size));
	std::memcpy(expected.data(), &size, sizeof(size));
	expected.insert(expected.end(), payload.begin(), payload.end());
	const auto first = expected;
	expected.insert(expected.end(), first.begin(), first.end());
	// Local write completion is not a promise of immediate peer delivery.
	const auto writeAllocations = allocation->attempts();
	allocation->refuse(); // The receiver must not initiate another native endpoint write.
	std::vector<std::byte> actual(expected.size());
	boost::asio::steady_timer deadline(context, std::chrono::seconds(2));
	bool readCompleted = false, timerCompleted = false, timedOut = false;
	size_t received = 0;
	boost::system::error_code readError, timerError, cancelError, closeError;
	std::exception_ptr timerCancelFailure, receiveFailure;
	context.restart();
	try
	{
		deadline.async_wait([&](const boost::system::error_code & error)
		{
			timerCompleted = true;
			timerError = error;
			if(!readCompleted)
			{
				timedOut = true;
				peer.cancel(cancelError);
				peer.close(closeError); // Retire this exact read even if cancellation is unsupported.
			}
		});
		boost::asio::async_read(peer, boost::asio::buffer(actual),
			[&](const boost::system::error_code & error, size_t bytes)
			{
				readCompleted = true;
				readError = error;
				received = bytes;
				try { deadline.cancel(); }
				catch(...) { timerCancelFailure = std::current_exception(); }
			});
		context.run(); // Drain the read AND cancelled/expired timer before captures retire.
	}
	catch(...)
	{
		receiveFailure = std::current_exception();
		peer.cancel(cancelError);
		peer.close(closeError);
		try { deadline.cancel(); }
		catch(...) { timerCancelFailure = std::current_exception(); }
		context.restart();
		context.run(); // Cleanup only: no new read, write or retry.
	}
	EXPECT_FALSE(receiveFailure);
	ASSERT_TRUE(readCompleted);
	ASSERT_TRUE(timerCompleted);
	EXPECT_FALSE(timedOut);
	EXPECT_FALSE(readError);
	EXPECT_TRUE(!timerError || timerError == boost::asio::error::operation_aborted);
	EXPECT_FALSE(cancelError);
	EXPECT_FALSE(closeError);
	EXPECT_FALSE(timerCancelFailure);
	ASSERT_EQ(received, expected.size());
	EXPECT_EQ(actual, expected);
	EXPECT_EQ(allocation->attempts(), writeAllocations);
}

TEST_F(PreparedWriteFailureTest, ZeroNativeWriteResourceLimitRefusesBeforeAnyAcceptance)
{
	EXPECT_THROW(NetworkWriteAllocationControl(0), std::invalid_argument);
}
}
