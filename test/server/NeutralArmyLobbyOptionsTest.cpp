/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../server/CVCMIServer.h"
#include "../../server/LobbyNetPackVisitors.h"
#include "../../lib/mapping/CMapInfo.h"
#include "../../lib/mapping/CMapHeader.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include "../../lib/serializer/GameConnection.h"
#include <limits>

namespace
{
// Existing transport interface only, without opening a socket or starting an
// executor. Visitor admission tests must not send any transport bytes.
class LobbyOptionConnection final : public INetworkConnection
{
public:
	void sendPacket(const std::vector<std::byte> &) override { ADD_FAILURE() << "Unexpected transport write"; }
	void setAsyncWritesEnabled(bool) override {}
	void close() override {}
};
class NeutralArmyLobbyOptionsTest : public ::testing::Test
{
protected:
	CVCMIServer server{0, true};
	std::shared_ptr<LobbyOptionConnection> endpoint = std::make_shared<LobbyOptionConnection>();
	std::shared_ptr<GameConnection> connection = std::make_shared<GameConnection>(endpoint);
	void SetUp() override
	{
		connection->connectionID = GameConnectionID::FIRST_CONNECTION;
		server.hostClientId = connection->connectionID;
		server.si->mode = EStartMode::NEW_GAME;
	}
	bool submit(const ExtraOptionsInfo & options)
	{
		LobbySetExtraOptions pack;
		pack.extraOptionsInfo = options;
		ClientPermissionsCheckerNetPackVisitor permissions(server, connection);
		pack.visit(permissions);
		if(!permissions.getResult()) return false;
		ApplyOnServerNetPackVisitor apply(server, connection);
		pack.visit(apply);
		return apply.getResult();
	}
};
}
TEST_F(NeutralArmyLobbyOptionsTest, HostSetsAndClearsValidRequestWithoutChangingOtherFlags)
{
	for(const int32_t percent : {1, 50, 75, 100})
	{
		auto requested = server.si->extraOptionsInfo;
		requested.neutralArmySizePercent = percent;
		ASSERT_TRUE(submit(requested));
		EXPECT_EQ(server.si->extraOptionsInfo, requested);
	}
	auto absent = server.si->extraOptionsInfo;
	absent.neutralArmySizePercent.reset();
	ASSERT_TRUE(submit(absent));
	EXPECT_EQ(server.si->extraOptionsInfo, absent);
}
TEST_F(NeutralArmyLobbyOptionsTest, NonHostCannotApplyAnOtherwiseValidRequest)
{
	server.hostClientId = GameConnectionID(123);
	const auto before = server.si->extraOptionsInfo;
	auto requested = before;
	requested.neutralArmySizePercent = 75;
	EXPECT_FALSE(submit(requested));
	EXPECT_EQ(server.si->extraOptionsInfo, before);
}
TEST_F(NeutralArmyLobbyOptionsTest, InvalidValuesRejectBeforeInstallingAnyExtraFlags)
{
	for(const int32_t percent : {std::numeric_limits<int32_t>::min(), -1, 0, 101, std::numeric_limits<int32_t>::max()})
	{
		const auto before = server.si->extraOptionsInfo;
		auto requested = before;
		requested.neutralArmySizePercent = percent;
		requested.cheatsAllowed = !before.cheatsAllowed;
		requested.recordGame = !before.recordGame;
		EXPECT_FALSE(submit(requested));
		EXPECT_EQ(server.si->extraOptionsInfo, before);
	}
}
TEST_F(NeutralArmyLobbyOptionsTest, LoadedAndCampaignChoicesAreFrozenWhileUnchangedRequestAllowsOtherFlags)
{
	server.si->extraOptionsInfo.neutralArmySizePercent = 75;
	for(const auto mode : {EStartMode::LOAD_GAME, EStartMode::CAMPAIGN})
	{
		server.si->mode = mode;
		const auto before = server.si->extraOptionsInfo;
		auto requested = before;
		requested.neutralArmySizePercent = 50;
		requested.unlimitedReplay = !before.unlimitedReplay;
		EXPECT_FALSE(submit(requested));
		EXPECT_EQ(server.si->extraOptionsInfo, before);
		requested.neutralArmySizePercent = before.neutralArmySizePercent;
		ASSERT_TRUE(submit(requested));
		EXPECT_EQ(server.si->extraOptionsInfo, requested);
	}
}
TEST_F(NeutralArmyLobbyOptionsTest, GameplayAndBattleOnlyRejectChangedNeutralRequest)
{
	server.setState(EServerState::GAMEPLAY);
	const auto before = server.si->extraOptionsInfo;
	auto requested = before;
	requested.neutralArmySizePercent = 75;
	EXPECT_FALSE(submit(requested));
	EXPECT_EQ(server.si->extraOptionsInfo, before);
	server.setState(EServerState::LOBBY);
	server.mi = std::make_shared<CMapInfo>();
	server.mi->mapHeader = std::make_unique<CMapHeader>();
	server.mi->mapHeader->battleOnly = true;
	EXPECT_FALSE(submit(requested));
	EXPECT_EQ(server.si->extraOptionsInfo, before);
}
