/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "../../AI/BattleAI/SearchBudget.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

using namespace battleSearch;
namespace
{
constexpr size_t slot(WorkKind kind) { return static_cast<size_t>(kind); }
WorkAmounts allowance(WorkKind kind, uint32_t amount)
{
	WorkAmounts result{};
	result[slot(kind)] = amount;
	return result;
}
}

TEST(SearchBudgetTest, ExhaustionIsCheckedBeforeWorkAndFailedDebitsChangeNothing)
{
	SearchBudget budget(allowance(WorkKind::Node, 2));
	unsigned work = 0;
	for(unsigned attempt = 0; attempt != 10; ++attempt)
		if(budget.consume(WorkKind::Node)) ++work;
	EXPECT_EQ(work, 2u);
	EXPECT_EQ(budget.spent()[slot(WorkKind::Node)], 2u);
	EXPECT_EQ(budget.refusals()[slot(WorkKind::Node)], 8u);
	EXPECT_THROW(budget.require(WorkKind::Node), BudgetExhausted);
	EXPECT_EQ(budget.spent()[slot(WorkKind::Node)], 2u);
}

TEST(SearchBudgetTest, FamilyReservationCannotBeConsumedByEarlierGeometry)
{
	SearchBudget decision(allowance(WorkKind::Generation, 9));
	auto heal = decision.reserve(allowance(WorkKind::Generation, 3));
	ASSERT_TRUE(heal);
	unsigned geometry = 0;
	while(decision.consume(WorkKind::Generation)) ++geometry;
	EXPECT_EQ(geometry, 6u);
	EXPECT_TRUE(heal->consume(WorkKind::Generation, 3));
	EXPECT_EQ(decision.spent()[slot(WorkKind::Generation)], 9u);
	EXPECT_FALSE(heal->consume(WorkKind::Generation));
}

TEST(SearchBudgetTest, NestedReservationsChargeAncestorsOnceAndReleaseOnlyUnusedQuota)
{
	SearchBudget root(allowance(WorkKind::Node, 100));
	{
		auto family = root.reserve(allowance(WorkKind::Node, 80));
		ASSERT_TRUE(family);
		{
			auto candidate = family->reserve(allowance(WorkKind::Node, 60));
			ASSERT_TRUE(candidate);
			EXPECT_FALSE(family->reserve(allowance(WorkKind::Node, 21)));
			EXPECT_TRUE(candidate->consume(WorkKind::Node, 17));
			EXPECT_EQ(root.spent()[slot(WorkKind::Node)], 17u);
			EXPECT_EQ(family->spent()[slot(WorkKind::Node)], 17u);
			EXPECT_EQ(root.available()[slot(WorkKind::Node)], 20u);
		}
		EXPECT_EQ(family->available()[slot(WorkKind::Node)], 63u);
	}
	EXPECT_EQ(root.available()[slot(WorkKind::Node)], 83u);
}

TEST(SearchBudgetTest, ChildCanOutliveHandleToParentWithoutReleasingItsProtection)
{
	SearchBudget root(allowance(WorkKind::Node, 20));
	std::optional<SearchBudget> child;
	{
		auto parent = root.reserve(allowance(WorkKind::Node, 15));
		ASSERT_TRUE(parent);
		child = parent->reserve(allowance(WorkKind::Node, 10));
	}
	EXPECT_EQ(root.available()[slot(WorkKind::Node)], 5u);
	ASSERT_TRUE(child);
	EXPECT_TRUE(child->consume(WorkKind::Node, 2));
	child.reset();
	EXPECT_EQ(root.available()[slot(WorkKind::Node)], 18u);
}

TEST(SearchBudgetTest, InvalidKindsAndLargeUnsignedRequestsNeverOverflowOrPartiallyReserve)
{
	SearchBudget budget({10, 10, 10, 10, 10});
	EXPECT_FALSE(budget.consume(WorkKind::Node, std::numeric_limits<uint32_t>::max()));
	EXPECT_THROW(budget.consume(static_cast<WorkKind>(255)), std::invalid_argument);
	EXPECT_FALSE(budget.reserve({1, 1, 1, 11, 1}));
	EXPECT_EQ(budget.available(), (WorkAmounts{10, 10, 10, 10, 10}));
	EXPECT_TRUE(budget.consume(WorkKind::Node, 0));
	EXPECT_EQ(budget.spent(), WorkAmounts{});
}

TEST(SearchBudgetTest, NoUnknownFamilyCanTurnIntoAnUnlimitedContinuationBypass)
{
	std::vector<RefinementCandidate> candidates;
	for(size_t family = 0; family != familyCount; ++family)
		for(size_t aim = 0; aim != 200; ++aim)
			candidates.push_back({family * 200 + aim, static_cast<Family>(family), {}});
	const auto plan = refinementPlan(candidates);
	ASSERT_EQ(plan.size(), familyCount);
	for(size_t family = 0; family != familyCount; ++family) EXPECT_EQ(plan[family], family * 200);
	EXPECT_EQ(plan, refinementPlan(candidates));
}

TEST(SearchBudgetTest, StrongKnownScoutCannotConsumeReservedUnscoutableHealingAndSummonWork)
{
	std::vector<RefinementCandidate> candidates{
		{0, Family::Damage, 900.0f}, {1, Family::Damage, 500.0f},
		{2, Family::Damage, 300.0f}, {3, Family::Damage, 100.0f},
		{4, Family::Healing, {}}, {5, Family::Summon, {}},
		{6, Family::Control, {}}, {7, Family::Movement, {}}};
	EXPECT_EQ(refinementPlan(candidates), (std::vector<size_t>{0, 1, 2, 4, 5, 6, 7}));
}

TEST(SearchBudgetTest, StableTiesNonfiniteScoutsAndGenerationFamiliesUseOriginalOrdinals)
{
	std::vector<RefinementCandidate> candidates{
		{8, Family::Damage, 5.0f}, {2, Family::Damage, 5.0f},
		{7, Family::Healing, std::numeric_limits<float>::quiet_NaN()},
		{4, Family::Control, std::numeric_limits<float>::infinity()}};
	EXPECT_EQ(refinementPlan(candidates), (std::vector<size_t>{2, 4, 7, 8}));
	const std::vector<Family> families{Family::Damage, Family::Damage, Family::Healing,
		Family::Summon, Family::Damage, Family::Healing};
	EXPECT_EQ(generationOrder(families), (std::vector<size_t>{0, 2, 3, 1, 5, 4}));
}

TEST(SearchBudgetTest, FullKeepsOriginalPortfolioAndHasAnExplicitFiniteCeiling)
{
	const auto limits = SearchLimits::full();
	std::vector<RefinementCandidate> candidates;
	for(size_t i = 0; i != 600; ++i) candidates.push_back({i, Family::Control, {}});
	const auto plan = refinementPlan(candidates, limits);
	ASSERT_EQ(plan.size(), 512u);
	for(size_t i = 0; i != plan.size(); ++i) EXPECT_EQ(plan[i], i);
	for(const auto limit : limits.decision) EXPECT_GT(limit, 0u);
}

TEST(SearchBudgetTest, AbandonedPartialCandidateCannotReplaceAnAlreadyLegalIncumbent)
{
	// Exercise the required transaction boundary: only a fully completed
	// continuation may commit utility; an exhausted callback leaves its detached
	// work and partial optimistic value unselected.
	SearchBudget budget(allowance(WorkKind::Node, 2));
	unsigned incumbent = 17;
	float incumbentValue = 4;
	for(unsigned ordinal = 0; ordinal != 3; ++ordinal)
		try
		{
			budget.require(WorkKind::Node);
			float candidate = 100;
			budget.require(WorkKind::Node);
			candidate = -1;
			if(candidate > incumbentValue) { incumbent = ordinal; incumbentValue = candidate; }
		}
		catch(const BudgetExhausted &) {}
	EXPECT_EQ(incumbent, 17u);
	EXPECT_EQ(incumbentValue, 4);
}

TEST(SearchBudgetTest, ReservationRejectsOvercommitAndRefinementNeverExceedsItsContinuationAllowance)
{
	SearchLimits limits;
	limits.decision[slot(WorkKind::Continuation)] = 2;
	std::vector<RefinementCandidate> candidates{
		{0, Family::Healing, {}}, {1, Family::Control, {}}, {2, Family::Damage, 1000.0f}};
	EXPECT_EQ(refinementPlan(candidates, limits), (std::vector<size_t>{0, 1}));
	SearchBudget root(allowance(WorkKind::Continuation, 2));
	auto reserved = root.reserve(allowance(WorkKind::Continuation, 2));
	ASSERT_TRUE(reserved);
	EXPECT_FALSE(root.reserve(allowance(WorkKind::Continuation, 1)));
	EXPECT_FALSE(root.consume(WorkKind::Continuation));
}

TEST(SearchBudgetTest, DiscoveryAndScoutingCannotSpendReservedContinuationWork)
{
	const SearchLimits limits;
	SearchBudget root(limits.decision);
	HeroWork phases(root, limits);
	while(root.consume(WorkKind::Generation)) {}
	while(root.consume(WorkKind::Node)) {}
	while(root.consume(WorkKind::Movement)) {}
	auto candidate = phases.reserveContinuation();
	ASSERT_TRUE(candidate);
	EXPECT_TRUE(candidate->consume(WorkKind::Continuation));
	EXPECT_TRUE(candidate->consume(WorkKind::Generation, limits.continuation[0]));
	EXPECT_TRUE(candidate->consume(WorkKind::Node, limits.continuation[3]));
	EXPECT_TRUE(candidate->consume(WorkKind::Movement, limits.continuation[4]));
	const auto spent = root.spent();
	for(size_t i = 0; i != workKindCount; ++i) EXPECT_LE(spent[i], limits.decision[i]);
}

TEST(SearchBudgetTest, FamilyReservationsRemainFiniteEvenWithZeroRemainingDiscoveryWork)
{
	SearchBudget root(WorkAmounts{});
	FamilyReservations families(&root, WorkKind::Generation, 1024, true);
	for(size_t i = 0; i != familyCount; ++i)
	{
		auto * family = families.forFamily(static_cast<Family>(i));
		ASSERT_NE(family, nullptr);
		EXPECT_FALSE(family->consume(WorkKind::Generation));
	}
	EXPECT_EQ(root.spent(), WorkAmounts{});
}

TEST(SearchBudgetTest, NativeSpellGroupingPreservesReferenceQuotaForEachDifficultFamily)
{
	EXPECT_EQ(spellFamily("new-horizons:forcedRecall", false, false, false), Family::Movement);
	EXPECT_EQ(spellFamily("new-horizons:summonLivingArmor", false, true, false), Family::Summon);
	EXPECT_EQ(spellFamily("new-horizons:reanimate", false, true, true), Family::Healing);
	EXPECT_EQ(spellFamily("core:quicksand", false, false, false), Family::Obstacle);
	EXPECT_EQ(spellFamily("core:magicArrow", true, false, false), Family::Damage);
	EXPECT_EQ(spellFamily("core:haste", false, true, false), Family::Enhancement);
	EXPECT_EQ(spellFamily("new-horizons:unknownNativeControl", false, false, false), Family::Control);
}

TEST(SearchBudgetTest, ReservationExhaustionIsReportedAsARefusalWithoutSpendingResources)
{
	SearchBudget root({0, 0, 1, 10, 0});
	EXPECT_FALSE(root.reserve({0, 0, 1, 11, 0}));
	EXPECT_EQ(root.refusals()[3], 1u);
	EXPECT_EQ(root.spent(), WorkAmounts{});
}
