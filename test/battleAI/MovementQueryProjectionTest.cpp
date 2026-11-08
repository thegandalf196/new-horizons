/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/MovementQueryCache.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/CRandomGenerator.h"
#include <tuple>

namespace
{
void expectSameReachability(const ReachabilityInfo & actual, const ReachabilityInfo & expected)
{
	EXPECT_EQ(actual.distances, expected.distances);
	EXPECT_EQ(actual.predecessors, expected.predecessors);
	EXPECT_EQ(static_cast<const TAccessibilityArray &>(actual.accessibility),
		static_cast<const TAccessibilityArray &>(expected.accessibility));
	EXPECT_EQ(actual.accessibility.demonicGateReservationCounts, expected.accessibility.demonicGateReservationCounts);
	EXPECT_EQ(actual.accessibility.demonicGateReservationBase, expected.accessibility.demonicGateReservationBase);
	for(int hex = 0; hex < GameConstants::BFIELD_SIZE; ++hex)
		EXPECT_EQ(actual.isReachable(BattleHex(hex)), expected.isReachable(BattleHex(hex)));
}

class RecordingMovementCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;
	RecordingMovementCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override { submitted.push_back(action); }
};

class MovementQueryProjectionTest : public ProjectedTargetHitFixture
{};
}

TEST_F(MovementQueryProjectionTest, SiblingBranchesShareExactPathsAndChangedGeometryMisses)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto memo = std::make_shared<MovementQueryCache>();
	child->setMovementQueryCache(memo);
	auto left = std::make_shared<HypotheticBattle>(environment.get(), child);
	auto right = std::make_shared<HypotheticBattle>(environment.get(), child);
	ASSERT_EQ(left->movementQueryCache(), memo);
	ASSERT_EQ(right->movementQueryCache(), memo);
	const auto actorId = incoming->unitId();
	const auto original = left->getReachability(left->battleGetUnitByID(actorId));
	expectSameReachability(right->getReachability(right->battleGetUnitByID(actorId)), original);
	EXPECT_EQ(memo->statistics().solves, 1u);
	EXPECT_EQ(memo->statistics().hits, 1u);

	right->moveUnit(braced->unitId(), BattleHex(91));
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), right);
	reference->setMovementQueryCache(nullptr);
	const auto changed = right->getReachability(right->battleGetUnitByID(actorId));
	expectSameReachability(changed, reference->getReachability(reference->battleGetUnitByID(actorId)));
	EXPECT_NE(changed.distances, original.distances);
	EXPECT_EQ(memo->statistics().solves, 2u);
	expectSameReachability(left->getReachability(left->battleGetUnitByID(actorId)), original);
	EXPECT_EQ(memo->statistics().hits, 2u);
}

TEST_F(MovementQueryProjectionTest, CacheHitReturnsCurrentParametersAndDoesNotRetainBranch)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto memo = std::make_shared<MovementQueryCache>();
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), child);
	branch->setMovementQueryCache(memo);
	std::weak_ptr<HypotheticBattle> lifetime = branch;
	const auto * actor = branch->battleGetUnitByID(incoming->unitId());
	BattleHexArray firstAccessible = actor->getHexes();
	BattleHexArray secondAccessible = firstAccessible;
	ReachabilityInfo::Parameters first(actor, actor->getPosition());
	first.knownAccessible = &firstAccessible;
	auto second = first;
	second.knownAccessible = &secondAccessible;
	const auto firstResult = branch->getReachability(first);
	const auto secondResult = branch->getReachability(second);
	expectSameReachability(secondResult, firstResult);
	EXPECT_EQ(firstResult.params.knownAccessible, &firstAccessible);
	EXPECT_EQ(secondResult.params.knownAccessible, &secondAccessible);
	EXPECT_EQ(memo->statistics().hits, 1u);
	branch.reset();
	EXPECT_TRUE(lifetime.expired());
	EXPECT_EQ(memo->statistics().entries, 1u);
}

TEST_F(MovementQueryProjectionTest, EnabledAndDisabledEvaluatorKeepActionsStateAndRng)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	activate(braced->unitId());
	auto queryEnvironment = std::make_shared<projectedHitTest::Environment>(gameState());
	auto callback = std::make_shared<RecordingMovementCallback>();
	callback->onBattleStarted(battle());
	const auto stateBefore = gameState()->saveToMemory();
	const auto serverRng = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto gameRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto aiRng = projectedHitTest::serialized(CRandomGenerator::getDefault());
	auto disabled = std::make_shared<MovementQueryCache>(0);
	auto enabled = std::make_shared<MovementQueryCache>();
	const auto run = [&](const std::shared_ptr<MovementQueryCache> & cache)
	{
		callback->submitted.clear();
		BattleEvaluator evaluator(queryEnvironment, callback, braced, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2, cache);
		const auto action = evaluator.selectStackAction(braced);
		const bool cast = evaluator.canCastSpell() && evaluator.attemptCastingSpell(braced, false);
		return std::tuple{action, cast, callback->submitted};
	};
	const auto reference = run(disabled);
	const auto optimized = run(enabled);
	EXPECT_EQ(optimized, reference);
	EXPECT_EQ(enabled->statistics().queries, disabled->statistics().queries);
	EXPECT_GT(enabled->statistics().hits, 0u);
	EXPECT_LT(enabled->statistics().solves, disabled->statistics().solves);
	EXPECT_EQ(gameState()->saveToMemory(), stateBefore);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverRng);
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), aiRng);
}
