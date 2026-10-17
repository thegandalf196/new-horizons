/*
 * PreparedGameConnectionState.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "PreparedGameConnectionBatch.h"
#include "GameConnection.h"
#include "BinarySerializer.h"
#include "../network/PreparedNetworkBatch.h"
#include <thread>

struct PreparedGameConnectionBatch::State
{
	struct Writer final : IBinaryWriter
	{
		std::vector<std::byte> bytes;
		size_t maximum = bytes.max_size();
		int write(const std::byte * data, unsigned size) override;
	};
	struct Encoder
	{
		std::shared_ptr<GameConnection> connection;
		GameConnectionID id;
		std::string uuid;
		NetworkConnectionPtr endpoint;
		Writer writer;
		std::unique_ptr<BinarySerializer> staged;
	};
	NetworkContext & publisher;
	const std::vector<std::shared_ptr<GameConnection>> & recipients;
	const std::shared_ptr<GameConnection> * data;
	size_t capacity;
	std::vector<std::shared_ptr<GameConnection>> occurrences;
	std::vector<std::unique_lock<std::mutex>> locks;
	std::vector<std::unique_ptr<Encoder>> encoders;
	std::unique_ptr<BattleUnitsChanged> packet;
	std::unique_ptr<PreparedNetworkBatch> outbox;
	std::thread::id thread = std::this_thread::get_id();
	bool accepted = false;
	State(NetworkContext & publisher, const std::vector<std::shared_ptr<GameConnection>> & recipients);
	void lockConnections();
	void encode(const BattleUnitsChanged & packet);
};
