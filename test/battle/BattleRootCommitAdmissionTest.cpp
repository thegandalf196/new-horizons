/*
 * BattleRootCommitAdmissionTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../lib/gameState/BattleRootLifetime.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include <atomic>
#include <future>
#include <stdexcept>
#include <thread>
#include <type_traits>

namespace
{
using Admission = BattleRootLifetime::CommitAdmission;
static_assert(noexcept(std::declval<const BattleRootLifetime::Pin &>().tryCommit()));
static_assert(!std::is_move_constructible_v<Admission> && !std::is_copy_constructible_v<Admission>);
static_assert(std::is_nothrow_destructible_v<Admission>);

class RetainedBattle final : public BattleInfo
{
	std::atomic<int> & destroyed;
public:
	explicit RetainedBattle(std::atomic<int> & destroyed) : BattleInfo(nullptr), destroyed(destroyed) {}
	~RetainedBattle() override { ++destroyed; }
};

class BattleRootCommitAdmissionTest : public HeroCommandFixture
{
protected:
	std::atomic<int> destroyed{0};
	std::shared_ptr<CGameState> world()
	{
		auto result = std::make_shared<CGameState>();
		result->day = 3;
		result->currentBattles.push_back(std::make_unique<RetainedBattle>(destroyed));
		return result;
	}
	std::shared_ptr<Bonus> speed(int value)
	{
		return std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
			BonusSource::OTHER, value, BonusSourceID());
	}
	std::unique_ptr<PreparedBonusGraphStorage> replacement(CBonusSystemNode & node)
	{
		node.addNewBonus(speed(2));
		EXPECT_EQ(query(node), 2); // Capture the actual warmed cache identity, not its old stamp.
		auto graph = std::make_unique<PreparedBonusGraphStorage>();
		const auto index = graph->capture(node);
		auto values = graph->projected(index);
		values.accepted.clear();
		values.exported.clear();
		auto next = speed(9);
		values.accepted.push_back(next);
		values.exported.push_back(next);
		graph->replace(index, std::move(values));
		graph->addInvalidation(true, {&node});
		graph->prepare();
		return graph;
	}
	int query(const CBonusSystemNode & node)
	{
		return node.getAllBonuses(Selector::type()(BonusType::STACKS_SPEED), "root-commit-speed")->totalValue();
	}
};

TEST_F(BattleRootCommitAdmissionTest, RetireFirstAndNestedTemporaryScopeRefuseCasWithoutAnyExchange)
{
	auto state = world();
	auto pin = BattleRootLifetime::retain(state);
	CBonusSystemNode node(BonusNodeType::STACK_BATTLE, true);
	auto graph = replacement(node);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	std::promise<void> ready, releaseFirst, firstClosed, releaseLast;
	auto admitted = ready.get_future(), firstDone = firstClosed.get_future();
	auto firstRelease = releaseFirst.get_future(), lastRelease = releaseLast.get_future();
	std::exception_ptr error;
	std::thread worker([&]
	{
		try
		{
			auto last = BattleRootLifetime::retire(state);
			{
				auto first = BattleRootLifetime::retire(state);
				ready.set_value();
				firstRelease.wait();
			}
			firstClosed.set_value();
			lastRelease.wait();
		}
		catch(...) { error = std::current_exception(); }
	});
	EXPECT_EQ(admitted.wait_for(std::chrono::seconds(1)), std::future_status::ready);
	{ auto gate = pin->tryCommit(); EXPECT_FALSE(static_cast<bool>(gate)); }
	releaseFirst.set_value();
	EXPECT_EQ(firstDone.wait_for(std::chrono::seconds(1)), std::future_status::ready);
	{ auto gate = pin->tryCommit(); EXPECT_FALSE(static_cast<bool>(gate)); }
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_EQ(query(node), 2);
	EXPECT_EQ(state->currentBattles.size(), 1);
	releaseLast.set_value();
	worker.join();
	EXPECT_FALSE(error);
	bool committed = false;
	{ auto gate = pin->tryCommit(); ASSERT_TRUE(static_cast<bool>(gate)); committed = graph->commit(); }
	EXPECT_TRUE(committed);
	EXPECT_EQ(query(node), 9);
}

TEST_F(BattleRootCommitAdmissionTest, CommitFirstRetirementObservesOnlyFinishedExchangeAndRetainsRoots)
{
	auto state = world();
	auto pin = BattleRootLifetime::retain(state);
	CBonusSystemNode node(BonusNodeType::STACK_BATTLE, true);
	auto graph = replacement(node);
	std::vector<int> replay{2}, preparedReplay{9};
	std::promise<void> entering, entered;
	auto attempted = entering.get_future(), admitted = entered.get_future();
	std::atomic<int> exchanged{0}, observed{0};
	bool committed = false;
	std::exception_ptr error;
	std::thread worker;
	{
		auto gate = pin->tryCommit();
		ASSERT_TRUE(static_cast<bool>(gate));
		worker = std::thread([&]
		{
			entering.set_value();
			try
			{
				auto retirement = BattleRootLifetime::retire(state, BattleRootLifetime::RetirementMode::PERMANENT);
				observed.store(exchanged.load());
				entered.set_value();
				auto mutation = retirement.mutate();
				state->currentBattles.clear();
			}
			catch(...) { error = std::current_exception(); }
		});
		EXPECT_EQ(attempted.wait_for(std::chrono::seconds(1)), std::future_status::ready);
		EXPECT_EQ(admitted.wait_for(std::chrono::milliseconds(10)), std::future_status::timeout);
		committed = graph->commit(); // Real isolated owned graph CAS, no fixture const_cast.
		static_assert(noexcept(replay.swap(preparedReplay)));
		if(committed)
		{
			replay.swap(preparedReplay); // Representative preallocated no-query exchange tail.
			exchanged.store(1);
		}
	}
	EXPECT_TRUE(committed); // No observer/test formatting is run inside the successful tail.
	const auto retirementStatus = admitted.wait_for(std::chrono::seconds(1));
	EXPECT_EQ(destroyed.load(), 0); // Actual root mutation still waits for origin Pin.
	pin.reset(); // Always release before joining; never transfer thread-affine unlock.
	worker.join();
	EXPECT_EQ(retirementStatus, std::future_status::ready);
	EXPECT_FALSE(error);
	EXPECT_EQ(observed.load(), 1);
	EXPECT_EQ(replay, std::vector<int>{9});
	EXPECT_EQ(query(node), 9);
	EXPECT_EQ(destroyed.load(), 1);
	EXPECT_THROW((void)BattleRootLifetime::retain(state), std::logic_error);
}

TEST_F(BattleRootCommitAdmissionTest, RefusedNestedCommitCannotReleaseTheOwningAdmission)
{
	auto state = world();
	auto pin = BattleRootLifetime::retain(state);
	{
		auto first = pin->tryCommit();
		ASSERT_TRUE(static_cast<bool>(first));
		{ auto nested = pin->tryCommit(); EXPECT_FALSE(static_cast<bool>(nested)); }
		{ auto stillNested = pin->tryCommit(); EXPECT_FALSE(static_cast<bool>(stillNested)); }
		EXPECT_THROW((BattleRootLifetime::retire(state, BattleRootLifetime::RetirementMode::PERMANENT)), std::logic_error);
		EXPECT_TRUE(pin->admitted());
	}
	{ auto next = pin->tryCommit(); EXPECT_TRUE(static_cast<bool>(next)); }
}

TEST_F(BattleRootCommitAdmissionTest, StaleGraphRefusalReleasesAdmissionWithoutPublishingTail)
{
	auto state = world();
	auto pin = BattleRootLifetime::retain(state);
	CBonusSystemNode node(BonusNodeType::STACK_BATTLE, true);
	auto graph = replacement(node);
	node.nodeHasChanged();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	bool tail = false;
	{
		auto gate = pin->tryCommit();
		ASSERT_TRUE(static_cast<bool>(gate));
		if(graph->commit())
			tail = true;
	}
	EXPECT_FALSE(tail);
	EXPECT_EQ(query(node), 2);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	{ auto next = pin->tryCommit(); EXPECT_TRUE(static_cast<bool>(next)); }
	pin.reset();
	EXPECT_NO_THROW((BattleRootLifetime::retire(state)));
}

TEST_F(BattleRootCommitAdmissionTest, ExceptionalPreCasScopeReleasesAdmissionForAnotherPreparation)
{
	auto state = world();
	auto pin = BattleRootLifetime::retain(state);
	EXPECT_THROW(( [&]
	{
		auto gate = pin->tryCommit();
		if(!gate)
			throw std::runtime_error("Unexpected admission refusal");
		throw std::logic_error("Preparation failure before CAS");
	}() ), std::logic_error);
	{ auto next = pin->tryCommit(); EXPECT_TRUE(static_cast<bool>(next)); }
	EXPECT_EQ(state->currentBattles.size(), 1);
	EXPECT_EQ(destroyed.load(), 0);
}
}
