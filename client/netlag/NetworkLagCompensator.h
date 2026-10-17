/*
 * NetworkLagCompensator.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once
#include "NetworkLagReplyState.h"

#include "../../lib/network/NetworkInterface.h"
#include "../../server/IGameServer.h"

struct CPackForServer;
class CGameState;
class GameConnection;

class NetworkLagReplyPrediction;
class PreparedClientBattleReplay;
struct BattleUnitsChanged;

/// Fake server that is used by client to make a quick prediction on what real server would reply without waiting for network latency
class NetworkLagCompensator final : public IGameServer, public INetworkConnectionListener, boost::noncopyable
{
	NetworkLagReplyState replyState;
	std::shared_ptr<INetworkConnection> antilagNetConnection;
	std::shared_ptr<GameConnection> antilagGameConnection;
	std::shared_ptr<CGameState> gameState;


	// IGameServer impl
	void setState(EServerState value) override;
	EServerState getState() const override;
	bool isPlayerHost(const PlayerColor & color) const override;
	bool hasPlayerAt(PlayerColor player, GameConnectionID connection) const override;
	bool hasBothPlayersAtSameConnection(PlayerColor left, PlayerColor right) const override;
	void applyPack(CPackForClient & pack) override;
	void sendPack(CPackForClient & pack, GameConnectionID connectionID) override;

	void onDisconnected(const std::shared_ptr<INetworkConnection> & connection, const std::string & errorMessage) override;
	void onPacketReceived(const std::shared_ptr<INetworkConnection> & connection, const std::vector<std::byte> & message) override;

public:
	NetworkLagCompensator(INetworkHandler & network, const std::shared_ptr<CGameState> & gs);
	~NetworkLagCompensator();

	void tryPredictReply(const CPackForServer & request);
	bool verifyReply(const CPackForClient & reply);
	std::unique_ptr<PreparedNetworkLagRetirement> prepareReplyRetirement(
		const BattleReplyInspection & inspection, const std::unique_lock<std::mutex> & interfaceAccess);
	std::unique_ptr<PreparedClientBattleReplay> prepareBattleReplayEntry(
		const std::shared_ptr<CGameState> & world, BattleUnitsChanged & packet) const;
};
