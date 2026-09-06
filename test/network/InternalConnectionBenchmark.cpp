/*
 * InternalConnectionBenchmark.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../../lib/network/NetworkInterface.h"
#include "../../lib/network/PerformanceTrace.h"
#include "../../lib/serializer/CMemorySerializer.h"

#include <cstdlib>
#include <iostream>
#include <numeric>

namespace
{
using Clock = nhperf::Clock;

enum class Variant { THREADED_BYTES, QUEUED_BYTES, DIRECT_BYTES, DIRECT_TYPED };
const char * name(Variant variant)
{
	switch(variant)
	{
		case Variant::THREADED_BYTES: return "production_threaded_bytes";
		case Variant::QUEUED_BYTES: return "production_same_thread_queued_bytes";
		case Variant::DIRECT_BYTES: return "harness_same_thread_direct_bytes";
		case Variant::DIRECT_TYPED: return "harness_same_thread_direct_typed";
	}
	throw std::logic_error("Unknown variant");
}

bool enabled()
{
	const auto * value = std::getenv("NH_RUN_TRANSPORT_BENCHMARK");
	return value && std::string(value) == "1";
}

// Production decoding intentionally rejects uint64_t. Preserve every state/sequence
// bit using two supported unsigned 32-bit fields; arithmetic remains modulo 2^64.
// Saving must not mutate the request/response passed through the serializer.
template<typename Handler> void serializeUnsignedBits(Handler & h, uint64_t & value)
{
	uint32_t low = static_cast<uint32_t>(value);
	uint32_t high = static_cast<uint32_t>(value >> 32);
	h & low;
	h & high;
	if constexpr(!Handler::saving)
		value = (static_cast<uint64_t>(high) << 32) | low;
}

// Deliberately synthetic validated work, NOT a game command or full-game dispatch.
// Byte modes use VCMI's real serializer and connection/listener dispatch. Typed
// mode removes encoding explicitly but performs identical validation/state work.
struct Request
{
	uint64_t seq = 0;
	int32_t amount = 0;
	std::vector<uint8_t> payload;
	template<typename Handler> void serialize(Handler & h)
	{
		serializeUnsignedBits(h, seq);
		h & amount;
		h & payload;
	}
};

struct Response
{
	uint64_t seq = 0;
	bool accepted = false;
	uint64_t state = 0;
	template<typename Handler> void serialize(Handler & h)
	{
		serializeUnsignedBits(h, seq);
		h & accepted;
		serializeUnsignedBits(h, state);
	}
	bool operator==(const Response &) const = default;
};

Response validatedOperation(const Request & request, uint64_t & state)
{
	bool valid = request.amount > 0 && request.amount <= 3;
	uint64_t work = 0;
	for(auto byte : request.payload)
	{
		valid = valid && byte == 0x5a;
		work = work * 33 + byte;
	}
	if(valid)
	{
		for(int i = 0; i < 32; ++i)
			work = work * 1664525 + 1013904223;
		state += work + request.amount;
	}
	return {request.seq, valid, state};
}

template<typename T> std::vector<std::byte> encode(const T & value)
{
	CMemorySerializer serializer;
	serializer.oser & value;
	return serializer.extractBuffer();
}

template<typename T> T decode(const std::vector<std::byte> & bytes)
{
	CMemorySerializer serializer(bytes);
	T value;
	serializer.iser & value;
	return value;
}

// Each slot is written by one producer per phase, before handing the message on.
// Completion is published under mutex; the controller reads only after completion.
// Delivery includes payload copy, enqueue, queue wait and production listener dispatch;
// it is NOT a pure queue-wait measurement. Typed mode omits byte phases explicitly.
struct Sample
{
	uint64_t start = 0, requestReady = 0, responseReady = 0;
	uint64_t encode = 0, requestQueue = 0, decode = 0, work = 0;
	uint64_t responseEncode = 0, responseQueue = 0, responseDecode = 0, rtt = 0;
};

class Session
{
	class ServerListener final : public INetworkServerListener
	{
		Session & owner;
	public:
		explicit ServerListener(Session & owner) : owner(owner) {}
		void onNewConnection(const NetworkConnectionPtr & connection) override { owner.serverConnection = connection; }
		void onDisconnected(const NetworkConnectionPtr &, const std::string &) override {}
		void onPacketReceived(const NetworkConnectionPtr &, const std::vector<std::byte> & bytes) override { owner.receiveRequest(bytes); }
	} serverListener;
	class ClientListener final : public INetworkClientListener
	{
		Session & owner;
	public:
		explicit ClientListener(Session & owner) : owner(owner) {}
		void onConnectionEstablished(const NetworkConnectionPtr & connection) override { owner.clientConnection = connection; }
		void onConnectionFailed(const std::string & message) override { throw std::runtime_error(message); }
		void onDisconnected(const NetworkConnectionPtr &, const std::string &) override {}
		void onPacketReceived(const NetworkConnectionPtr &, const std::vector<std::byte> & bytes) override { owner.receiveResponse(bytes); }
	} clientListener;

	Variant variant;
	std::unique_ptr<INetworkHandler> serverNetwork = INetworkHandler::createHandler();
	std::unique_ptr<INetworkHandler> clientNetwork = INetworkHandler::createHandler();
	std::unique_ptr<INetworkServer> server;
	NetworkConnectionPtr serverConnection, clientConnection;
	std::thread serverWorker, clientWorker;
	std::thread::id serverThread, clientThread;
	std::mutex mutex;
	std::condition_variable completion;
	std::exception_ptr failure;
	std::vector<Request> requests;
	std::vector<Response> expected;
	std::vector<Sample> samples;
	uint64_t state = 0, expectedState = 0, nextSeq = 0, baseSeq = 0;
	size_t completed = 0;
	bool instrumented = false, reenter = false;
	unsigned dispatchDepth = 0;

	bool queued() const { return variant == Variant::THREADED_BYTES || variant == Variant::QUEUED_BYTES; }
	uint64_t stamp() const { return instrumented ? nhperf::nowNs() : 0; }

	void worker(INetworkHandler & network)
	{
		try { network.run(); }
		catch(...)
		{
			{
				std::lock_guard lock(mutex);
				failure = std::current_exception();
			}
			completion.notify_all();
		}
	}

	void await(size_t count)
	{
		const auto deadline = Clock::now() + std::chrono::seconds(10);
		if(variant == Variant::THREADED_BYTES)
		{
			std::unique_lock lock(mutex);
			if(!completion.wait_until(lock, deadline, [&] { return completed >= count || failure; }))
				throw std::runtime_error("Response deadline exceeded");
			if(failure)
				std::rethrow_exception(failure);
		}
		else
		{
			while(completed < count)
			{
				if(Clock::now() >= deadline)
					throw std::runtime_error("Same-thread response deadline exceeded");
				serverNetwork->getContext().restart();
				clientNetwork->getContext().restart();
				serverNetwork->getContext().poll();
				clientNetwork->getContext().poll();
			}
		}
	}

	void send(size_t index)
	{
		auto & sample = samples.at(index);
		sample.start = stamp();
		if(variant == Variant::DIRECT_TYPED)
		{
			sample.requestReady = stamp();
			dispatch(requests.at(index));
			return;
		}
		auto bytes = encode(requests.at(index));
		sample.requestReady = stamp();
		sample.encode = sample.requestReady - sample.start;
		if(queued())
			clientConnection->sendPacket(bytes);
		else
			serverListener.onPacketReceived({}, bytes);
	}

	void receiveRequest(const std::vector<std::byte> & bytes)
	{
		const auto received = stamp();
		const auto request = decode<Request>(bytes);
		const auto decoded = stamp();
		auto & sample = samples.at(request.seq - baseSeq);
		sample.requestQueue = received - sample.requestReady;
		sample.decode = decoded - received;
		dispatch(request);
	}

	void dispatch(const Request & request)
	{
		if(std::this_thread::get_id() != serverThread || request.seq != nextSeq++)
			throw std::runtime_error("Authoritative callback thread/order mismatch");
		++dispatchDepth;
		maximumDispatchDepth = std::max(maximumDispatchDepth, dispatchDepth);
		auto & sample = samples.at(request.seq - baseSeq);
		const auto workStart = stamp();
		const auto response = validatedOperation(request, state);
		const auto workEnd = stamp();
		sample.work = workEnd - workStart;
		if(variant == Variant::DIRECT_TYPED)
		{
			sample.responseReady = stamp();
			deliver(response);
		}
		else
		{
			auto bytes = encode(response);
			sample.responseReady = stamp();
			sample.responseEncode = sample.responseReady - workEnd;
			if(queued())
				serverConnection->sendPacket(bytes);
			else
				clientListener.onPacketReceived({}, bytes);
		}
		--dispatchDepth;
	}

	void receiveResponse(const std::vector<std::byte> & bytes)
	{
		const auto received = stamp();
		const auto response = decode<Response>(bytes);
		const auto decoded = stamp();
		auto & sample = samples.at(response.seq - baseSeq);
		sample.responseQueue = received - sample.responseReady;
		sample.responseDecode = decoded - received;
		deliver(response);
	}

	void deliver(const Response & response)
	{
		const auto index = response.seq - baseSeq;
		if(std::this_thread::get_id() != clientThread || response != expected.at(index))
			throw std::runtime_error("Response thread/result/state mismatch");
		samples.at(index).rtt = stamp() - samples.at(index).start;
		const bool submitNested = reenter && index == 0;
		{
			std::lock_guard lock(mutex);
			if(index != completed++)
				throw std::runtime_error("Response order mismatch");
		}
		// A correctness probe, never enabled for timing. Direct variants re-enter
		// server dispatch here; this explicitly prevents inferring full-game safety.
		if(submitNested)
			send(1);
		completion.notify_all();
	}

public:
	unsigned maximumDispatchDepth = 0;

	explicit Session(Variant variant) : serverListener(*this), clientListener(*this), variant(variant)
	{
		serverThread = clientThread = std::this_thread::get_id();
		if(queued())
		{
			server = serverNetwork->createServerTCP(serverListener); // wrapper only: NEVER start/listen
			clientNetwork->createInternalConnection(clientListener, *server);
			// Complete setup before starting workers; do not race on connection pointers.
			serverNetwork->getContext().poll();
			clientNetwork->getContext().poll();
			if(!serverConnection || !clientConnection)
				throw std::runtime_error("Production internal connection setup failed");
			serverNetwork->getContext().restart();
			clientNetwork->getContext().restart();
		}
		if(variant == Variant::THREADED_BYTES)
		{
			serverWorker = std::thread([this] { worker(*serverNetwork); });
			try { clientWorker = std::thread([this] { worker(*clientNetwork); }); }
			catch(...) { serverNetwork->stop(); serverWorker.join(); throw; }
			serverThread = serverWorker.get_id();
			clientThread = clientWorker.get_id();
		}
	}

	~Session()
	{
		serverNetwork->stop();
		clientNetwork->stop();
		if(serverWorker.joinable()) serverWorker.join();
		if(clientWorker.joinable()) clientWorker.join();
	}

	std::pair<uint64_t, std::vector<Sample>> run(size_t count, size_t payload, size_t burst, bool trace, bool probeReentry = false)
	{
		// Previous await publishes all response writes. All expected work is outside timing.
		instrumented = trace;
		reenter = probeReentry;
		baseSeq += requests.size();
		requests.clear(); expected.clear(); samples.assign(count, {});
		completed = 0;
		for(size_t index = 0; index < count; ++index)
		{
			Request request{baseSeq + index, (baseSeq + index) % 7 == 0 ? -1 : 1, std::vector<uint8_t>(payload, 0x5a)};
			expected.push_back(validatedOperation(request, expectedState));
			requests.push_back(std::move(request));
		}
		const auto start = nhperf::nowNs();
		if(probeReentry)
		{
			if(count != 2) throw std::logic_error("Reentry probe requires two requests");
			send(0);
			await(2);
		}
		else
		{
			for(size_t index = 0; index < count;)
			{
				const auto end = std::min(count, index + burst);
				for(; index < end; ++index) send(index);
				await(end);
			}
		}
		const auto elapsed = nhperf::nowNs() - start;
		return {elapsed, samples};
	}
};

void printMetrics(const char * field, std::vector<uint64_t> values)
{
	std::sort(values.begin(), values.end());
	const auto percentile = [&](size_t percent) { return values.at((values.size() - 1) * percent / 100); };
	std::cout << ",\"" << field << "\":{\"p50\":" << percentile(50) << ",\"p95\":" << percentile(95)
		<< ",\"p99\":" << percentile(99) << ",\"max\":" << values.back() << ",\"sum\":"
		<< std::accumulate(values.begin(), values.end(), uint64_t(0)) << '}';
}
}

TEST(InternalConnectionBenchmark, UnsignedWireBoundaryRoundTrips)
{
	if(!enabled()) GTEST_SKIP() << "Opt-in experimental wire representation check";
	for(uint64_t value : {uint64_t(0), uint64_t(1) << 32, uint64_t(1) << 63, std::numeric_limits<uint64_t>::max()})
	{
		const Request request{value, -1, {0x5a}};
		const auto loadedRequest = decode<Request>(encode(request));
		EXPECT_EQ(loadedRequest.seq, value);
		EXPECT_EQ(loadedRequest.amount, request.amount);
		EXPECT_EQ(loadedRequest.payload, request.payload);
		EXPECT_EQ(request.seq, value);
		const Response response{value, false, value};
		EXPECT_EQ(decode<Response>(encode(response)), response);
		EXPECT_EQ(response.seq, value);
		EXPECT_EQ(response.state, value);
	}
}

TEST(InternalConnectionBenchmark, PayloadBurstSweep)
{
	if(!enabled()) GTEST_SKIP() << "Set NH_RUN_TRANSPORT_BENCHMARK=1 in an exclusive timing window";
	constexpr size_t repetitions = 10;
	constexpr size_t messages = 256;
	const std::array variants = {Variant::THREADED_BYTES, Variant::QUEUED_BYTES, Variant::DIRECT_BYTES, Variant::DIRECT_TYPED};
	for(size_t payload : {size_t(0), size_t(64), size_t(4096), size_t(65536)})
		for(size_t burst : {size_t(1), size_t(16), size_t(64)})
			for(size_t repetition = 0; repetition < repetitions; ++repetition)
				for(size_t variantIndex = 0; variantIndex < variants.size(); ++variantIndex)
					for(bool trace : {false, true})
					{
						// Rotate variant order across repetitions rather than always warming baseline first.
						const auto variant = variants[(variantIndex + repetition) % variants.size()];
						std::pair<uint64_t, std::vector<Sample>> result;
						try
						{
							Session session(variant);
							session.run(32, payload, burst, trace);
							result = session.run(messages, payload, burst, trace);
						}
						catch(...)
						{
							std::cout << "NH_PERF_FAILURE {\"variant\":\"" << name(variant) << "\",\"payload_bytes\":" << payload
								<< ",\"burst\":" << burst << ",\"repetition\":" << repetition << ",\"instrumented\":"
								<< (trace ? "true" : "false") << "}\n";
							throw; // Preserve the actual exception/deadline in gtest output; do not skip failed runs.
						}
						const auto & [elapsed, samples] = result;
						std::cout << "NH_PERF_MICRO {\"schema\":\"nh-perf-v1\",\"variant\":\"" << name(variant)
							<< "\",\"payload_bytes\":" << payload << ",\"burst\":" << burst << ",\"repetition\":" << repetition
							<< ",\"instrumented\":" << (trace ? "true" : "false") << ",\"count\":" << messages
							<< ",\"elapsed_ns\":" << elapsed << ",\"correct\":true,\"dropped_records\":0";
						if(trace)
						{
							const auto metric = [&](const char * field, auto member)
							{
								std::vector<uint64_t> values;
								for(const auto & sample : samples) values.push_back(sample.*member);
								printMetrics(field, std::move(values));
							};
							metric("encode_ns", &Sample::encode); metric("request_delivery_ns", &Sample::requestQueue);
							metric("decode_ns", &Sample::decode); metric("validation_work_ns", &Sample::work);
							metric("response_encode_ns", &Sample::responseEncode); metric("response_delivery_ns", &Sample::responseQueue);
							metric("response_decode_ns", &Sample::responseDecode); metric("rtt_ns", &Sample::rtt);
							std::cout << ",\"raw_rtt_ns\":[";
							for(size_t index = 0; index < samples.size(); ++index)
								std::cout << (index ? "," : "") << samples[index].rtt;
							std::cout << ']';
						}
						std::cout << "}\n"; // One buffered export per repetition, outside timed batch.
					}
}

TEST(InternalConnectionBenchmark, ReentryIsNotEquivalentToQueuedDispatch)
{
	if(!enabled()) GTEST_SKIP() << "Opt-in experimental correctness probe";
	for(auto variant : {Variant::THREADED_BYTES, Variant::QUEUED_BYTES, Variant::DIRECT_BYTES, Variant::DIRECT_TYPED})
	{
		Session session(variant);
		session.run(2, 64, 1, false, true);
		const auto expectedDepth = variant == Variant::DIRECT_BYTES || variant == Variant::DIRECT_TYPED ? 2u : 1u;
		EXPECT_EQ(session.maximumDispatchDepth, expectedDepth);
		std::cout << "NH_PERF_CORRECTNESS {\"variant\":\"" << name(variant) << "\",\"maximum_dispatch_depth\":"
			<< session.maximumDispatchDepth << ",\"full_game_safety_proven\":false}\n";
	}
}

TEST(InternalConnectionBenchmark, ProductionQueuedCloseDropsPendingPacket)
{
	if(!enabled()) GTEST_SKIP() << "Opt-in experimental cancellation probe";
	struct Listener : INetworkConnectionListener
	{
		unsigned packets = 0, disconnects = 0;
		void onPacketReceived(const NetworkConnectionPtr &, const std::vector<std::byte> &) override { ++packets; }
		void onDisconnected(const NetworkConnectionPtr &, const std::string &) override { ++disconnects; }
	} listener;
	auto network = INetworkHandler::createHandler();
	auto connection = network->createAsyncConnection(listener);
	connection->sendPacket({std::byte(1)});
	connection->close();
	network->getContext().poll();
	EXPECT_EQ(listener.packets, 0u);
	EXPECT_EQ(listener.disconnects, 1u);
	connection->sendPacket({std::byte(2)});
	network->getContext().restart();
	network->getContext().poll();
	EXPECT_EQ(listener.packets, 0u);
	network->stop(); network->stop();
}
