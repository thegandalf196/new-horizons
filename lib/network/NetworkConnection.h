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
#ifdef NH_PERF_EXPERIMENTS
#include <atomic>
#endif

class NetworkConnection final : public INetworkConnection, public std::enable_shared_from_this<NetworkConnection>
{
	static const int messageHeaderSize = sizeof(uint32_t);
	static const int messageMaxSize = 64 * 1024 * 1024; // arbitrary size to prevent potential massive allocation if we receive garbage input

	std::list<std::vector<std::byte>> dataToSend;
	std::shared_ptr<NetworkSocket> socket;
	std::shared_ptr<NetworkTimer> timer;
	std::mutex writeMutex;

	NetworkBuffer readBuffer;
	INetworkConnectionListener & listener;
	bool asyncWritesEnabled = false;

	void heartbeat();
	void onError(const std::string & message);

	void startReceiving();
	void onHeaderReceived(const boost::system::error_code & ec);
	void onPacketReceived(const boost::system::error_code & ec, uint32_t expectedPacketSize);

	void doSendData();
	void onDataSent(const boost::system::error_code & ec);

public:
	NetworkConnection(INetworkConnectionListener & listener, const std::shared_ptr<NetworkSocket> & socket, NetworkContext & context);

	void start();
	void close() override;
	void sendPacket(const std::vector<std::byte> & message) override;
	void setAsyncWritesEnabled(bool on) override;
};

class InternalConnection final : public IInternalConnection, public std::enable_shared_from_this<InternalConnection>
{
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
