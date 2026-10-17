/*
 * NativeBonusGraphAccessTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/bonuses/NativeBonusGraphAccess.h"
#include "../../lib/bonuses/CBonusSystemNode.h"
#include "../../lib/bonuses/BonusCache.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include <atomic>
#include <future>
#include <thread>
#include <type_traits>

namespace
{
std::shared_ptr<Bonus> speed(int value)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED,
		BonusSource::OTHER, value, BonusSourceID());
}

class ObservedBearer final : public IBonusBearer
{
public:
	mutable std::atomic<bool> entered{false};
	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & cachingStr = std::string()) const override
	{
		entered.store(true);
		auto result = std::make_shared<BonusList>();
		result->push_back(speed(7));
		return result;
	}
	TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override
	{
		return getAllBonuses(selector);
	}
	int32_t getTreeVersion() const override { return 1; }
};

class CopyingUpdater final : public IUpdater
{
public:
	std::shared_ptr<Bonus> createUpdatedBonus(const std::shared_ptr<Bonus> & value,
		const BonusEvaluationContext &) const override
	{
		return std::make_shared<Bonus>(*value);
	}
};
}

TEST(NativeBonusGraphAccessTest, PropagatedAddRefreshesWarmSourceAndOtherRecipientWithoutChangingHeldLists)
{
	CBonusSystemNode root(BonusNodeType::BATTLE_WIDE);
	CBonusSystemNode source(BonusNodeType::STACK_BATTLE), recipient(BonusNodeType::STACK_BATTLE);
	source.attachTo(root);
	recipient.attachTo(root);
	auto first = speed(2), second = speed(3);
	first->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::BATTLE_WIDE);
	second->propagator = first->propagator;
	source.addNewBonus(first);
	const auto held = recipient.getUnstackedBonuses(Selector::all);
	BonusValueCache sourceCache(&source, Selector::type()(BonusType::STACKS_SPEED));
	BonusValueCache recipientCache(&recipient, Selector::type()(BonusType::STACKS_SPEED));
	ASSERT_EQ(sourceCache.getValue(), 2);
	ASSERT_EQ(recipientCache.getValue(), 2);
	const auto generation = root.getTreeVersion();
	source.addNewBonus(second);
	EXPECT_NE(root.getTreeVersion(), generation);
	EXPECT_EQ(sourceCache.getValue(), 5);
	EXPECT_EQ(recipientCache.getValue(), 5);
	EXPECT_EQ(recipient.getUnstackedBonuses(Selector::all)->size(), 2);
	EXPECT_EQ(held->totalValue(), 2);
}

TEST(NativeBonusGraphAccessTest, PropagatedUpdaterRemovalRefreshesWarmSourceAndOtherRecipient)
{
	CBonusSystemNode root(BonusNodeType::BATTLE_WIDE);
	CBonusSystemNode source(BonusNodeType::STACK_BATTLE), recipient(BonusNodeType::STACK_BATTLE);
	source.attachTo(root);
	recipient.attachTo(root);
	auto aura = speed(7);
	aura->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::BATTLE_WIDE);
	aura->propagationUpdater = std::make_shared<CopyingUpdater>();
	source.addNewBonus(aura);
	const auto held = recipient.getUnstackedBonuses(Selector::all);
	ASSERT_EQ(held->size(), 1);
	EXPECT_NE(held->front(), aura);
	ASSERT_EQ(source.valOfBonuses(BonusType::STACKS_SPEED), 7);
	ASSERT_EQ(recipient.valOfBonuses(BonusType::STACKS_SPEED), 7);
	const auto generation = root.getTreeVersion();
	source.removeBonus(aura);
	EXPECT_NE(root.getTreeVersion(), generation);
	EXPECT_EQ(source.valOfBonuses(BonusType::STACKS_SPEED), 0);
	EXPECT_EQ(recipient.valOfBonuses(BonusType::STACKS_SPEED), 0);
	EXPECT_TRUE(recipient.getUnstackedBonuses(Selector::all)->empty());
	EXPECT_EQ(held->totalValue(), 7);
}

TEST(NativeBonusGraphAccessTest, NestedNativeQueriesAndMutationKeepExistingValues)
{
	CBonusSystemNode source(BonusNodeType::CREATURE), child(BonusNodeType::STACK_BATTLE);
	NativeBonusGraphAccess access;
	{
		NativeBonusGraphAccess nested;
		source.addNewBonus(speed(2));
		child.attachTo(source);
		EXPECT_EQ(child.valOfBonuses(BonusType::STACKS_SPEED), 2);
	}
	source.addNewBonus(speed(3));
	EXPECT_EQ(child.valOfBonuses(BonusType::STACKS_SPEED), 5);
	child.detachFrom(source);
	EXPECT_EQ(child.valOfBonuses(BonusType::STACKS_SPEED), 0);
}

TEST(NativeBonusGraphAccessTest, NativeWriterCannotChangeListsInsideAnotherThreadsInterval)
{
	CBonusSystemNode node(BonusNodeType::CREATURE);
	node.addNewBonus(speed(2));
	std::promise<void> started;
	auto start = started.get_future();
	std::future<void> done;
	std::thread writer;
	{
		NativeBonusGraphAccess access;
		auto completion = std::make_shared<std::promise<void>>();
		done = completion->get_future();
		writer = std::thread([&node, &started, completion]
		{
			started.set_value();
			try { node.addNewBonus(speed(9)); completion->set_value(); }
			catch(...) { completion->set_exception(std::current_exception()); }
		});
		start.wait();
		EXPECT_EQ(done.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
		EXPECT_EQ(node.valOfBonuses(BonusType::STACKS_SPEED), 2);
	}
	writer.join();
	EXPECT_NO_THROW(done.get());
	EXPECT_EQ(node.valOfBonuses(BonusType::STACKS_SPEED), 11);
}

TEST(NativeBonusGraphAccessTest, InterfaceValueConsumerOwnsIntervalBeforeCallingAnArbitraryBearer)
{
	ObservedBearer bearer;
	std::promise<void> started;
	auto start = started.get_future();
	std::future<int> done;
	std::thread reader;
	{
		NativeBonusGraphAccess access;
		auto result = std::make_shared<std::promise<int>>();
		done = result->get_future();
		reader = std::thread([&bearer, &started, result]
		{
			started.set_value();
			try { result->set_value(bearer.valOfBonuses(BonusType::STACKS_SPEED)); }
			catch(...) { result->set_exception(std::current_exception()); }
		});
		start.wait();
		EXPECT_EQ(done.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
		EXPECT_FALSE(bearer.entered.load());
	}
	reader.join();
	EXPECT_EQ(done.get(), 7);
	EXPECT_TRUE(bearer.entered.load());
}

TEST(NativeBonusGraphAccessTest, WarmCacheHitAlsoWaitsBeforeReturningItsValue)
{
	CBonusSystemNode node(BonusNodeType::CREATURE);
	node.addNewBonus(speed(6));
	BonusValueCache cache(&node, Selector::type()(BonusType::STACKS_SPEED));
	ASSERT_EQ(cache.getValue(), 6);
	std::promise<void> started;
	auto start = started.get_future();
	std::future<int> done;
	std::thread reader;
	{
		NativeBonusGraphAccess access;
		auto result = std::make_shared<std::promise<int>>();
		done = result->get_future();
		reader = std::thread([&cache, &started, result]
		{
			started.set_value();
			try { result->set_value(cache.getValue()); }
			catch(...) { result->set_exception(std::current_exception()); }
		});
		start.wait();
		EXPECT_EQ(done.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
	}
	reader.join();
	EXPECT_EQ(done.get(), 6);
}

TEST(NativeBonusGraphAccessTest, ScopeCannotMoveToAnotherUnlockingThread)
{
	static_assert(!std::is_copy_constructible_v<NativeBonusGraphAccess>);
	static_assert(!std::is_move_constructible_v<NativeBonusGraphAccess>);
	static_assert(std::is_nothrow_destructible_v<NativeBonusGraphAccess>);
}
