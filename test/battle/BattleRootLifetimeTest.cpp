/*
 * BattleRootLifetimeTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../lib/gameState/BattleRootLifetime.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/gameState/GameStatePackVisitor.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include <atomic>
#include <future>
#include <thread>
#include <type_traits>

namespace
{
class TrackedBattle final : public BattleInfo
{
	std::atomic<int> & destroyed;
public:
	explicit TrackedBattle(std::atomic<int> & destroyed) : BattleInfo(nullptr), destroyed(destroyed) {}
	~TrackedBattle() override { ++destroyed; }
};

class BattleRootLifetimeTest : public HeroCommandFixture
{
protected:
	std::atomic<int> destroyed{0};
	std::shared_ptr<CGameState> makeWorld()
	{
		auto world = std::make_shared<CGameState>();
		world->day = 3;
		world->currentBattles.push_back(std::make_unique<TrackedBattle>(destroyed));
		return world;
	}
};

TEST_F(BattleRootLifetimeTest, SameThreadRetirementRefusesBeforeAnyRootOrAdmissionChange)
{
	auto world = makeWorld();
	auto * original = world->currentBattles.front().get();
	auto pin = BattleRootLifetime::retain(world);
	EXPECT_TRUE(pin->admitted());
	EXPECT_THROW((BattleRootLifetime::retire(world, BattleRootLifetime::RetirementMode::PERMANENT)), std::logic_error);
	EXPECT_EQ(world->currentBattles.front().get(), original);
	EXPECT_EQ(destroyed.load(), 0);
	EXPECT_TRUE(pin->admitted()); // Failed admission rolled back; never marked permanent.
	pin.reset();
	EXPECT_NO_THROW((void)BattleRootLifetime::retain(world));
}

TEST_F(BattleRootLifetimeTest, PinStronglyRetainsWorldAndNativeBattleUntilItsOwnUnlock)
{
	auto world = makeWorld();
	std::weak_ptr<CGameState> weak = world;
	auto pin = BattleRootLifetime::retain(world);
	world.reset();
	EXPECT_FALSE(weak.expired());
	EXPECT_EQ(destroyed.load(), 0);
	pin.reset(); // Must unlock/decrement before the final world runs its guarded destructor.
	EXPECT_TRUE(weak.expired());
	EXPECT_EQ(destroyed.load(), 1);
}

TEST_F(BattleRootLifetimeTest, NestedTemporaryRetirementsReopenOnlyAfterLastScope)
{
	auto world = makeWorld();
	{
		auto outer = BattleRootLifetime::retire(world);
		EXPECT_THROW((void)BattleRootLifetime::retain(world), std::logic_error);
		{
			auto inner = BattleRootLifetime::retire(world);
			auto mutation = inner.mutate();
			EXPECT_THROW((void)BattleRootLifetime::retain(world), std::logic_error);
		}
		EXPECT_THROW((void)BattleRootLifetime::retain(world), std::logic_error);
	}
	EXPECT_NO_THROW((void)BattleRootLifetime::retain(world));
	EXPECT_EQ(world->currentBattles.size(), 1);
}

TEST_F(BattleRootLifetimeTest, PermanentRetirementDoesNotReopenWithTheTemporaryScope)
{
	auto world = makeWorld();
	{
		auto temporary = BattleRootLifetime::retire(world);
		{ auto permanent = BattleRootLifetime::retire(world, BattleRootLifetime::RetirementMode::PERMANENT); }
		EXPECT_THROW((void)BattleRootLifetime::retain(world), std::logic_error);
	}
	EXPECT_THROW((void)BattleRootLifetime::retain(world), std::logic_error);
	EXPECT_EQ(world->currentBattles.size(), 1); // Admission alone never destroys roots.
}

TEST_F(BattleRootLifetimeTest, RetirementRetainsWorldAcrossClientStyleSharedOwnerReset)
{
	auto world = makeWorld();
	std::weak_ptr<CGameState> weak = world;
	{
		auto retirement = BattleRootLifetime::retire(world, BattleRootLifetime::RetirementMode::PERMANENT);
		{
			auto mutation = retirement.mutate();
			world->currentBattles.clear();
		}
		world.reset();
		EXPECT_FALSE(weak.expired());
		EXPECT_EQ(destroyed.load(), 1);
	}
	EXPECT_TRUE(weak.expired());
}

TEST_F(BattleRootLifetimeTest, CrossThreadAdmissionDoesNotWaitForPinAndMutationWaitsUntilUnlock)
{
	auto world = makeWorld();
	auto pin = BattleRootLifetime::retain(world);
	std::promise<void> admitted, startMutation, complete;
	auto admission = admitted.get_future(), start = startMutation.get_future(), done = complete.get_future();
	std::exception_ptr error;
	std::mutex simulatedInterface;
	std::unique_lock ui(simulatedInterface); // This owner is NOT released just to let a pin retire.
	std::thread worker([&]
	{
		try
		{
			auto retirement = BattleRootLifetime::retire(world);
			admitted.set_value();
			start.wait();
			auto mutation = retirement.mutate();
			world->currentBattles.clear();
		}
		catch(...) { error = std::current_exception(); }
		complete.set_value();
	});
	const auto admissionStatus = admission.wait_for(std::chrono::seconds(1));
	EXPECT_FALSE(pin->admitted());
	startMutation.set_value();
	const auto beforeRelease = done.wait_for(std::chrono::milliseconds(10));
	EXPECT_EQ(destroyed.load(), 0);
	pin.reset(); // Always unblock BEFORE joining/asserting fatal conditions.
	worker.join();
	EXPECT_EQ(admissionStatus, std::future_status::ready);
	EXPECT_EQ(beforeRelease, std::future_status::timeout);
	EXPECT_FALSE(error);
	EXPECT_EQ(destroyed.load(), 1);
	EXPECT_TRUE(ui.owns_lock());
	EXPECT_NO_THROW((void)BattleRootLifetime::retain(world));
}

TEST_F(BattleRootLifetimeTest, AdmissionHoldsNoRootLockAcrossStopAndJoin)
{
	auto world = makeWorld();
	std::promise<void> pinned, stop;
	auto ready = pinned.get_future(), stopped = stop.get_future();
	std::exception_ptr error;
	std::thread worker([&]
	{
		std::unique_ptr<BattleRootLifetime::Pin> pin;
		try { pin = BattleRootLifetime::retain(world); }
		catch(...) { error = std::current_exception(); }
		pinned.set_value();
		stopped.wait();
	});
	ready.wait();
	if(error)
	{
		stop.set_value();
		worker.join();
		FAIL() << "Worker root admission unexpectedly failed";
	}
	{
		auto retirement = BattleRootLifetime::retire(world);
		stop.set_value();
		worker.join(); // Would deadlock if admission held root mutex or waited for this pin.
		auto mutation = retirement.mutate();
		world->currentBattles.clear();
	}
	EXPECT_EQ(destroyed.load(), 1);
}

TEST_F(BattleRootLifetimeTest, AnotherWorldCanRetireWithoutBorrowingThePinnedWorld)
{
	auto first = makeWorld(), second = makeWorld();
	auto pin = BattleRootLifetime::retain(first);
	{
		auto retirement = BattleRootLifetime::retire(second);
		auto mutation = retirement.mutate();
		second->currentBattles.clear();
	}
	EXPECT_EQ(first->currentBattles.size(), 1);
	EXPECT_TRUE(pin->admitted());
	EXPECT_EQ(destroyed.load(), 1);
}

TEST_F(BattleRootLifetimeTest, ConcurrentTemporaryRetirementDoesNotReopenWhenOneScopeEnds)
{
	auto world = makeWorld();
	std::promise<void> admitted, finish;
	auto ready = admitted.get_future(), release = finish.get_future();
	std::exception_ptr error;
	std::thread worker;
	{
		auto first = BattleRootLifetime::retire(world);
		worker = std::thread([&]
		{
			try
			{
				auto second = BattleRootLifetime::retire(world);
				admitted.set_value();
				release.wait();
			}
			catch(...)
			{
				error = std::current_exception();
				admitted.set_exception(error);
			}
		});
		ready.wait();
	}
	EXPECT_THROW((void)BattleRootLifetime::retain(world), std::logic_error);
	finish.set_value();
	worker.join();
	EXPECT_NO_THROW(ready.get());
	EXPECT_FALSE(error);
	EXPECT_NO_THROW((void)BattleRootLifetime::retain(world));
}

TEST_F(BattleRootLifetimeTest, NativeLoadRefusesPinnedRootsBeforeReadingItsSerializedInput)
{
	auto world = makeWorld();
	auto * original = world->currentBattles.front().get();
	auto pin = BattleRootLifetime::retain(world);
	EXPECT_THROW(world->loadFromMemory({}), std::logic_error);
	EXPECT_EQ(world->currentBattles.front().get(), original);
	EXPECT_EQ(destroyed.load(), 0);
}

TEST_F(BattleRootLifetimeTest, RealTemporarySnapshotLoadReopensPreparedPinAdmission)
{
	startGame();
	const auto snapshot = gameHandler->gs->saveToMemory();
	auto restored = std::make_shared<CGameState>();
	restored->preInit(LIBRARY);
	restored->loadFromMemory(snapshot);
	auto pin = BattleRootLifetime::retain(restored);
	EXPECT_TRUE(pin->admitted());
	EXPECT_EQ(restored->day, gameHandler->gs->day);
}

TEST_F(BattleRootLifetimeTest, RealCancelledVisitorRefusesBeforeItsNativeRootAndSideEffects)
{
	startGame();
	startBattle();
	auto world = gameHandler->gs;
	world->replayLog = ReplayLog(); // Isolate the visitor gate; outer replay ordering is NOT changed by this API.
	auto * original = battle();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	auto pin = BattleRootLifetime::retain(world);
	BattleCancelled packet;
	packet.battleID = original->getBattleID();
	EXPECT_THROW(world->apply(packet), std::logic_error);
	EXPECT_EQ(world->getBattle(packet.battleID), original);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(BattleRootLifetimeTest, RealEndedVisitorRefusesBeforeEraseAndReconciliationThenEndsAfterRelease)
{
	startGame();
	startBattle();
	auto world = gameHandler->gs;
	world->replayLog = ReplayLog(); // Isolate the same existing outer replay boundary.
	auto * original = battle();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	BattleEnded packet;
	packet.battleID = original->getBattleID();
	auto pin = BattleRootLifetime::retain(world);
	EXPECT_THROW(world->apply(packet), std::logic_error);
	ASSERT_EQ(world->currentBattles.size(), 1);
	ASSERT_EQ(world->currentBattles.front().get(), original);
	GameStatePackVisitor rejected(*world);
	EXPECT_THROW(packet.visitTyped(rejected), std::logic_error);
	EXPECT_FALSE(rejected.needsSpellPointReconciliation());
	EXPECT_EQ(world->getBattle(packet.battleID), original);
	EXPECT_EQ(world->currentBattles.size(), 1);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_TRUE(pin->admitted());
	pin.reset();
	GameStatePackVisitor accepted(*world);
	EXPECT_NO_THROW(packet.visitTyped(accepted));
	EXPECT_TRUE(accepted.needsSpellPointReconciliation());
	EXPECT_TRUE(world->currentBattles.empty());
	EXPECT_NO_THROW((void)BattleRootLifetime::retain(world));
}

TEST_F(BattleRootLifetimeTest, InvalidOwnerRefusesAndScopesCannotMoveAcrossThreads)
{
	static_assert(!std::is_move_constructible_v<BattleRootLifetime::Pin>);
	static_assert(!std::is_move_constructible_v<BattleRootLifetime::Mutation>);
	static_assert(!std::is_move_constructible_v<BattleRootLifetime::Retirement>);
	EXPECT_THROW((void)BattleRootLifetime::retain({}), std::invalid_argument);
	EXPECT_THROW((BattleRootLifetime::retire({})), std::invalid_argument);
}
}
