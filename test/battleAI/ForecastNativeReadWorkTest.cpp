/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapterFixture.h"
#include "../../AI/BattleAI/ForecastNativeReadView.h"
#include <limits>
#include <stdexcept>
namespace
{
std::vector<forecast::NativeReadOccurrence> occurrences(const forecast::State & state)
{
	std::vector<forecast::NativeReadOccurrence> result;
	for(const auto id : state.orderedIds())
	{
		const auto * value = state.unit(id);
		result.push_back({state.definition(id)->readDescriptor, state.readView(id)->valuesOwner(),
			value->currentModifiers, value->queries, {}});
	}
	return result;
}
}
TEST_F(ForecastNativeAdapterTest, ExplicitDecisionReadWorkSurvivesTemporaryGroupsAndCountsForkEventsOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	const auto & state = *parent->forecastState();
	const auto environment = state.readView(state.orderedIds().front())->environmentOwner();
	auto sink = std::make_shared<forecast::NativeReadWork>();
	std::weak_ptr<const void> temporaryGroup;
	{
		auto views = forecast::NativeReadView::fromOwned(occurrences(state), environment, sink);
		temporaryGroup = views.front()->workIdentity();
		const auto count = views.front()->unit().getCount();
		EXPECT_EQ(views.front()->unit().getCount(), count); // Repeated read: no new materialization.
		auto left = forecast::NativeReadView::forkViews(views);
		auto right = forecast::NativeReadView::forkViews(views);
		EXPECT_EQ(left.front()->unit().getCount(), count);
		EXPECT_EQ(right.front()->unit().getCount(), count);
		EXPECT_EQ(right.front()->unit().getCount(), count);
#ifdef NH_PERF_EXPERIMENTS
		EXPECT_EQ(sink->materialized, 3u); // Three actual receivers, no group-count sum.
		EXPECT_EQ(sink->owningLoans, 0u); EXPECT_EQ(sink->queryRebindings, 0u);
#endif
	}
	EXPECT_TRUE(temporaryGroup.expired());
#ifdef NH_PERF_EXPERIMENTS
	EXPECT_EQ(sink->materialized, 3u); // Explicit sink outlives all temporary groups.
#endif
}
TEST_F(ForecastNativeAdapterTest, ExplicitDecisionReadWorkTracksActualLoanCountRefreshAndOwnedLifetime)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	const auto & state = *parent->forecastState();
	const auto id = state.orderedIds().front();
	const auto environment = state.readView(id)->environmentOwner();
	auto sink = std::make_shared<forecast::NativeReadWork>();
	std::weak_ptr<forecast::NativeReadWork> retained = sink;
	auto acquisition = forecast::NativeReadView::acquireFromOwned(occurrences(state), environment, id, sink);
	ASSERT_TRUE(acquisition.unit);
	const auto original = acquisition.unit->getCount();
#ifdef NH_PERF_EXPERIMENTS
	EXPECT_EQ(sink->materialized, 0u); EXPECT_EQ(sink->owningLoans, 1u);
#endif
	int64_t damage = acquisition.unit->getMaxHealth() + 1;
	acquisition.unit->damage(damage, false, battle::DamageProvenance::SPELL);
	EXPECT_LT(acquisition.unit->getCount(), original);
	const auto attack = acquisition.unit->getAttack(false);
	const auto rebound = sink->queryRebindings;
	EXPECT_EQ(acquisition.unit->getAttack(false), attack);
	EXPECT_EQ(sink->queryRebindings, rebound); // Stable count: no repeated rebind.
#ifdef NH_PERF_EXPERIMENTS
	EXPECT_GT(rebound, 0u);
	EXPECT_EQ(sink->queryRebindings, acquisition.owner->work().queryRebindings);
#endif
	sink.reset(); acquisition.owner.reset();
	EXPECT_FALSE(retained.expired()); // Native loan retains group and diagnostic owner.
	acquisition.unit.reset(); EXPECT_TRUE(retained.expired()); // No diagnostic ownership cycle.
}
TEST_F(ForecastNativeAdapterTest, ExplicitDecisionReadWorkSaturatesAndIsolatesDifferentDecisions)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	const auto & state = *parent->forecastState();
	const auto environment = state.readView(state.orderedIds().front())->environmentOwner();
	auto first = std::make_shared<forecast::NativeReadWork>();
	auto second = std::make_shared<forecast::NativeReadWork>();
	first->materialized = std::numeric_limits<uint64_t>::max() - 1;
	auto a = forecast::NativeReadView::fromOwned(occurrences(state), environment, first);
	auto b = forecast::NativeReadView::fromOwned(occurrences(state), environment, first);
	auto independent = forecast::NativeReadView::fromOwned(occurrences(state), environment, second);
	EXPECT_EQ(a.front()->unit().getCount(), b.front()->unit().getCount());
#ifdef NH_PERF_EXPERIMENTS
	EXPECT_EQ(first->materialized, std::numeric_limits<uint64_t>::max()); EXPECT_TRUE(first->saturated);
#else
	EXPECT_EQ(first->materialized, std::numeric_limits<uint64_t>::max() - 1); EXPECT_FALSE(first->saturated);
#endif
	EXPECT_EQ(second->materialized, 0u);
	EXPECT_EQ(independent.front()->unit().getCount(), a.front()->unit().getCount());
#ifdef NH_PERF_EXPERIMENTS
	EXPECT_EQ(second->materialized, 1u); EXPECT_FALSE(second->saturated);
#else
	EXPECT_EQ(second->materialized, 0u);
#endif
}
TEST_F(ForecastNativeAdapterTest, RefusedReadFactoryDoesNotInventDiagnosticEvents)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	const auto & state = *parent->forecastState();
	const auto environment = state.readView(state.orderedIds().front())->environmentOwner();
	auto sink = std::make_shared<forecast::NativeReadWork>();
	EXPECT_THROW(forecast::NativeReadView::fromOwned({}, environment, sink), std::invalid_argument);
	EXPECT_THROW(forecast::NativeReadView::acquireFromOwned(occurrences(state), environment,
		std::numeric_limits<uint32_t>::max(), sink), std::invalid_argument);
	EXPECT_EQ(sink->materialized, 0u); EXPECT_EQ(sink->owningLoans, 0u);
	EXPECT_EQ(sink->queryRebindings, 0u); EXPECT_FALSE(sink->saturated);
}
