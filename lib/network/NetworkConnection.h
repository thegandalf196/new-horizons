/*
 * NetworkConnection.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "NetworkDefines.h"
#include "ExperimentalSetFormation.h"
#include "NetworkWriteFailureInfo.h"
#include <atomic>

class NetworkWriteAllocationControl;
class PreparedNetworkBatch;
class PreparedGameConnectionBatch;

class DLL_LINKAGE NetworkConnection final : public INetworkConnection, public std::enable_shared_from_this<NetworkConnection>
{
	friend class PreparedNetworkBatch;
	friend class PreparedGameConnectionBatch;
	static const int messageHeaderSize = sizeof(uint32_t);
	static const int messageMaxSize = 64 * 1024 * 1024; // arbitrary size to prevent potential massive allocation if we receive garbage input

	std::list<std::vector<std::byte>> dataToSend;
	NetworkContext & context;
	std::shared_ptr<NetworkSocket> socket;
	std::shared_ptr<NetworkTimer> timer;
	std::mutex writeMutex;

	NetworkBuffer readBuffer;
	INetworkConnectionListener & listener;
	bool asyncWritesEnabled = false;
	bool writeInProgress = false;
	size_t writeOffset = 0;
	std::atomic<bool> writeTerminal{false};
	NetworkWriteFailureInfo writeFailure;
	const std::string writeFailureMessage = "Queued network write failed";
	std::shared_ptr<NetworkWriteAllocationControl> writeAllocationControl;
	bool trySendData() noexcept;
	void recordWriteFailure(NetworkWriteFailureInfo::Origin origin,
		std::exception_ptr cause, const boost::system::error_code & error) noexcept;
	void finishWriteFailure() noexcept;
	boost::system::error_code drainSynchronousWrites();
	void startPreparedWrites();

	void heartbeat();
	void onError(const std::string & message);

	void startReceiving();
	void onHeaderReceived(const boost::system::error_code & ec);
	void onPacketReceived(const boost::system::error_code & ec, uint32_t expectedPacketSize);

	void doSendData();
	void onDataSent(const boost::system::error_code & ec, size_t transferred);

public:
	NetworkConnection(INetworkConnectionListener & listener, const std::shared_ptr<NetworkSocket> & socket, NetworkContext & context);
	NetworkConnection(INetworkConnectionListener & listener, const std::shared_ptr<NetworkSocket> & socket,
		NetworkContext & context, std::shared_ptr<NetworkWriteAllocationControl> allocationControl);
	bool hasTerminalWriteFailure() const noexcept;
	NetworkWriteStatus writeStatus();

	void start();
	void close() override;
	void sendPacket(const std::vector<std::byte> & message) override;
	void setAsyncWritesEnabled(bool on) override;
};

class DLL_LINKAGE InternalConnection final : public IInternalConnection, public std::enable_shared_from_this<InternalConnection>
{
	friend class PreparedNetworkBatch;
	std::mutex deliveryMutex;
	NetworkContext::strand deliveryStrand;
	std::weak_ptr<IInternalConnection> otherSideWeak;
	NetworkContext & context;
	INetworkConnectionListener & listener;
	bool connectionActive = false;
#ifdef NH_PERF_EXPERIMENTS
	// Written once by connectTo, before exposure; never reset by close/disconnect.
	std::weak_ptr<IInternalConnection> experimentalPeer;
	bool experimentalPeerBound = false;
	const bool experimentalCapable;
	const std::weak_ptr<ExperimentalSetFormationLease> experimentalReceiver;
	std::atomic<bool> experimentalOpen{false};
#endif
public:
	InternalConnection(INetworkConnectionListener & listener, NetworkContext & context
#ifdef NH_PERF_EXPERIMENTS
		, const std::shared_ptr<ExperimentalSetFormationLease> & receiver = {}
#endif
	);
#ifdef NH_PERF_EXPERIMENTS
	ExperimentalSetFormationResult queueSetFormation(const SetFormation & pack);
#endif

	void receivePacket(const std::vector<std::byte> & message) override;
	void disconnect() override;
	void connectTo(std::shared_ptr<IInternalConnection> connection) override;
	void sendPacket(const std::vector<std::byte> & message) override;
	void setAsyncWritesEnabled(bool on) override;
	void close() override;
};
