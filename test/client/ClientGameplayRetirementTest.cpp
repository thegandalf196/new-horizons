/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../client/ClientGameplayRetirement.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{
// Executes the exact production outer composition with a controlled yielding
// client substitute. It does not simulate a CClient or rendered GUI acceptance.
struct YieldingClient
{
	struct Source { const YieldingClient * client; std::shared_ptr<int> epoch; };
	std::shared_ptr<int> epoch = std::make_shared<int>(0);
	std::vector<std::string> & events;
	std::function<bool()> onFinish = [] { return true; };
	std::function<bool()> onEnd = [] { return true; };
	bool ended = false;
	explicit YieldingClient(std::vector<std::string> & events) : events(events) {}
	Source retainGameplaySource() const { return {this, epoch}; }
	bool matchesGameplaySource(const Source & s) const { return this == s.client && epoch == s.epoch && !ended; }
	bool matchesRetiredGameplaySource(const Source & s) const { return this == s.client && epoch == s.epoch && ended; }
	void endNetwork() { events.emplace_back("stop"); }
	bool finishGameplay()
	{
		events.emplace_back("finish");
		auto callback = onFinish; // Callback can delete this; no member access follows.
		return callback();
	}
	bool endGame()
	{
		events.emplace_back("end");
		ended = true;
		auto callback = onEnd;
		return callback();
	}
};
}

TEST(ClientGameplayRetirementTest, NormalRestartAndQuitSequenceResetsOnlyTheOriginal)
{
	std::vector<std::string> events;
	auto client = std::make_unique<YieldingClient>(events);
	EXPECT_TRUE(retireClientGameplay(client, [&] { events.emplace_back("disconnect"); return true; }));
	EXPECT_EQ(client, nullptr);
	EXPECT_EQ(events, (std::vector<std::string>{"stop", "finish", "disconnect", "end"}));
}

TEST(ClientGameplayRetirementTest, ReplacementDuringFinishNeverReceivesOldEndGameOrReset)
{
	std::vector<std::string> events;
	auto client = std::make_unique<YieldingClient>(events);
	client->onFinish = [&] { client = std::make_unique<YieldingClient>(events); return true; };
	EXPECT_FALSE(retireClientGameplay(client, [&] { events.emplace_back("disconnect"); return true; }));
	ASSERT_NE(client, nullptr);
	EXPECT_FALSE(client->ended);
	EXPECT_EQ(events, (std::vector<std::string>{"stop", "finish"}));
}

TEST(ClientGameplayRetirementTest, ReplacementDuringDisconnectNeverReceivesEndGameOrReset)
{
	std::vector<std::string> events;
	auto client = std::make_unique<YieldingClient>(events);
	EXPECT_FALSE(retireClientGameplay(client, [&]
	{
		events.emplace_back("disconnect");
		client = std::make_unique<YieldingClient>(events);
		return true;
	}));
	ASSERT_NE(client, nullptr);
	EXPECT_FALSE(client->ended);
	EXPECT_EQ(events, (std::vector<std::string>{"stop", "finish", "disconnect"}));
}

TEST(ClientGameplayRetirementTest, ReplacementDuringEndGameIsNeverResetByTheOldOwner)
{
	std::vector<std::string> events;
	auto client = std::make_unique<YieldingClient>(events);
	client->onEnd = [&] { client = std::make_unique<YieldingClient>(events); return true; };
	EXPECT_FALSE(retireClientGameplay(client, [] { return true; }));
	ASSERT_NE(client, nullptr);
	EXPECT_FALSE(client->ended);
	EXPECT_EQ(events, (std::vector<std::string>{"stop", "finish", "end"}));
}

TEST(ClientGameplayRetirementTest, SessionReplacementOnTheSameClientStopsContinuation)
{
	std::vector<std::string> events;
	auto client = std::make_unique<YieldingClient>(events);
	client->onFinish = [&] { client->epoch = std::make_shared<int>(1); return true; };
	EXPECT_FALSE(retireClientGameplay(client, [] { return true; }));
	EXPECT_FALSE(client->ended);
	EXPECT_EQ(events, (std::vector<std::string>{"stop", "finish"}));
}

TEST(ClientGameplayRetirementTest, RefusedFinishStopsAllDestructiveContinuation)
{
	std::vector<std::string> events;
	auto client = std::make_unique<YieldingClient>(events);
	client->onFinish = [] { return false; };
	EXPECT_FALSE(retireClientGameplay(client, [] { return true; }));
	EXPECT_FALSE(client->ended);
	EXPECT_EQ(events, (std::vector<std::string>{"stop", "finish"}));
}
