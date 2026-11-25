/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/ForecastStateTransitions.h"
#include "../../AI/BattleAI/ForecastNativeAdapter.h"
#include "../../AI/BattleAI/SearchBudget.h"
#include <limits>
namespace
{
class ForecastStateTest : public ProjectedTargetHitFixture
{
protected:
	forecast::State root()
	{
		std::vector<forecast::UnitInput> units;
		for(const auto * original : {incoming, braced})
		{
			const auto unit = child->getForUpdate(original->unitId());
			auto definition = std::make_shared<forecast::UnitDefinition>();
			definition->id = unit->unitId();
			definition->creature = unit->creatureId();
			definition->originalOwner = unit->unitOwner();
			definition->side = unit->unitSide();
			definition->baseAmount = unit->unitBaseAmount();
			definition->maximumHealth = unit->getMaxHealth();
			definition->movement = unit->getMovementRange();
			definition->supported = 0;
			for(auto event : {forecast::Event::Damage, forecast::Event::Healing, forecast::Event::Move,
				forecast::Event::Activation, forecast::Event::Retaliation, forecast::Event::Round, forecast::Event::Strike})
				definition->supported |= forecast::eventBit(event);
			// Numerical handles can be absent for unsupported native species;
			// ledger correctness never invents a numerical projection.
			for(size_t mode = 0; mode < 2; ++mode)
				if(auto prepared = nativeDamage::prepareUnit(*unit, mode != 0))
					definition->numeric[mode] = std::make_shared<const nativeDamage::PreparedUnit>(*prepared);
			forecast::UnitDelta state(battle::CHealth::detached(unit->getHealth(), unit->getHealth().valueContext()));
			state.numeric = definition->numeric;
			state.activation.moved = unit->movedThisRound;
			state.activation.waited = unit->waitedThisTurn;
			state.activation.waiting = unit->waiting;
			state.activation.defending = unit->defending;
			state.activation.hadMorale = unit->hadMorale;
			state.activation.waitBonusUsed = unit->battlecraftWaitBonusUsed;
			state.activation.waitMasteryDoubled = unit->battlecraftWaitMasteryDoubled;
			state.position = unit->getPosition();
			state.facing = forecast::Facing::Right;
			state.controller = child->battleGetActionController(unit.get());
			state.activation.retaliation = {unit->counterAttacks.available(), unit->counterAttacks.total(), unit->counterAttacks.isLimited()};
			units.push_back({std::move(definition), std::move(state)});
		}
		const auto planned = units.front().definition->supported;
		return forecast::State::capture({child->getBattleID(), PlayerColor(0), child->battleGetRound(), child->getActivationSerial()},
			std::move(units), planned);
	}
	void sameHealth(const battle::CHealth & actual, const battle::CHealth & expected)
	{
		EXPECT_TRUE(actual.isDetached());
		EXPECT_EQ(actual.available(), expected.available());
		EXPECT_EQ(actual.getCount(), expected.getCount());
		EXPECT_EQ(actual.getFirstHPleft(), expected.getFirstHPleft());
		EXPECT_EQ(actual.getResurrected(), expected.getResurrected());
		EXPECT_EQ(actual.getUnusableRemains(), expected.getUnusableRemains());
		for(const auto provenance : {battle::DamageProvenance::OTHER, battle::DamageProvenance::PHYSICAL_CREATURE,
			battle::DamageProvenance::SPELL}) EXPECT_EQ(actual.getCasualtyCount(provenance), expected.getCasualtyCount(provenance));
	}
};
}
TEST_F(ForecastStateTest, ConsecutiveNativeHealthMutationsRetainForkAndSiblingIsolation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto parentState = root();
	auto branch = parentState.fork();
	auto sibling = parentState.fork();
	const auto id = incoming->unitId();
	auto expected = battle::CHealth::detached(child->getForUpdate(id)->getHealth(), child->getForUpdate(id)->getHealth().valueContext());
	for(const int64_t request : {17, 23, 61})
	{
		auto amount = request;
		expected.damage(amount, false, false, battle::DamageProvenance::SPELL, true);
		const auto event = forecast::Transitions::applyDamage(branch, id, request, battle::DamageProvenance::SPELL, false, nullptr);
		ASSERT_TRUE(event);
		EXPECT_EQ(event.receipt->appliedHealth, amount);
		sameHealth(branch.unit(id)->health, expected);
	}
	int64_t healing = 39;
	expected.heal(healing, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE);
	ASSERT_TRUE(forecast::Transitions::applyHealing(branch, id, 39, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE, nullptr));
	sameHealth(branch.unit(id)->health, expected);
	// Retain a snapshot after private writes, then detach once and continue
	// writing without another owner. Both paths preserve earlier observations.
	const auto held = branch;
	const auto heldHealth = held.unit(id)->health.available();
	const auto heldRevision = held.revision();
	for(const auto request : {3, 5})
	{
		ASSERT_TRUE(forecast::Transitions::applyDamage(branch, id, request,
			battle::DamageProvenance::SPELL, false, nullptr));
		EXPECT_EQ(held.unit(id)->health.available(), heldHealth);
		EXPECT_EQ(held.revision(), heldRevision);
		EXPECT_FALSE(branch.sameSnapshot(held));
	}
	EXPECT_EQ(branch.revision(), heldRevision + 2);
	EXPECT_EQ(branch.unit(id)->health.available(), heldHealth - 8);
	EXPECT_EQ(sibling.unit(id)->health.available(), parentState.unit(id)->health.available());
	EXPECT_EQ(incoming->getAvailableHealth(), parentState.unit(id)->health.available());
}
TEST_F(ForecastStateTest, DerivedFactsRefreshOnlyChangedUnitAndOutliveTheParentBranch)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto state = root();
	const auto id = incoming->unitId();
	const auto * before = state.prepared(id, false);
	ASSERT_NE(before, nullptr);
	auto branch = state.fork();
	ASSERT_TRUE(forecast::Transitions::applyDamage(branch, id, incoming->getMaxHealth() + 1,
		battle::DamageProvenance::SPELL, false, nullptr));
	const auto * current = branch.prepared(id, false);
	ASSERT_NE(current, nullptr);
	EXPECT_NE(current, before);
	EXPECT_EQ(current->unit.count, branch.unit(id)->health.getCount());
	EXPECT_EQ(current->unit.firstHealth, branch.unit(id)->health.getFirstHPleft());
	EXPECT_EQ(state.prepared(id, false), before);
	EXPECT_EQ(branch.prepared(braced->unitId(), false), state.prepared(braced->unitId(), false));
#ifdef NH_PERF_EXPERIMENTS
	EXPECT_EQ(branch.work().derivedRefreshes, 1u);
#endif
}
TEST_F(ForecastStateTest, RoundTransitionsMatchOrdinaryNativeFlagsIncludingFinalForecastRound)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto state = root();
	const auto id = incoming->unitId();
	ASSERT_TRUE(forecast::Transitions::consumeActivation(state, id, nullptr));
	ASSERT_TRUE(forecast::Transitions::consumeRetaliation(state, id, nullptr));
	const auto round = state.currentRound();
	ASSERT_TRUE(forecast::Transitions::advanceRound(state, false, nullptr));
	EXPECT_EQ(state.currentRound(), round + 1);
	EXPECT_EQ(state.unit(id)->activation.completed, 0u);
	EXPECT_FALSE(state.unit(id)->activation.moved);
	EXPECT_FALSE(state.unit(id)->activation.waiting);
	EXPECT_EQ(state.unit(id)->activation.retaliation.available, state.unit(id)->activation.retaliation.total);
	ASSERT_TRUE(forecast::Transitions::advanceRound(state, false, nullptr));
	EXPECT_EQ(state.currentRound(), round + 2);
}
TEST_F(ForecastStateTest, MalformedAndBudgetExhaustedTransitionsLeaveNumericalStateUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto state = root();
	const auto id = incoming->unitId();
	const auto hp = state.unit(id)->health.available();
	const auto revision = state.revision();
	EXPECT_FALSE(forecast::Transitions::applyDamage(state, id, -1, battle::DamageProvenance::SPELL, false, nullptr));
	battleSearch::SearchBudget empty({0, 0, 0, 0, 0});
	EXPECT_FALSE(forecast::Transitions::applyDamage(state, id, 10, battle::DamageProvenance::SPELL, false, &empty));
	EXPECT_FALSE(forecast::Transitions::advanceRound(state, false, &empty));
	EXPECT_EQ(state.unit(id)->health.available(), hp);
	EXPECT_EQ(state.revision(), revision);
}
TEST_F(ForecastStateTest, UnrepresentedPlansRefuseBeforeMutatingNativeSource)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto before = child->getForUpdate(incoming->unitId())->save();
	const auto result = forecast::NativeAdapter::capture(*child, forecast::eventBit(forecast::Event::Reaction));
	EXPECT_FALSE(result.state);
	EXPECT_EQ(result.failure, forecast::AdmissionFailure::UnsupportedEvents);
	EXPECT_EQ(child->getForUpdate(incoming->unitId())->save(), before);
}

TEST_F(ForecastStateTest, NativeOrdinaryStrikeAndRoundFlagsUseTheSameExtractedTransitions)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto source = child->getForUpdate(incoming->unitId());
	source->waitedThisTurn = true;
	source->hadMorale = true;
	source->movedThisRound = true;
	auto state = root();
	auto native = source->acquireState();
	native->afterAttack(false, true, true);
	ASSERT_TRUE(forecast::Transitions::afterAttack(state, incoming->unitId(), false, true, true, nullptr));
	EXPECT_EQ(state.unit(incoming->unitId())->activation.retaliation.available, native->counterAttacks.available());
	EXPECT_EQ(state.unit(incoming->unitId())->activation.waitBonusUsed, native->battlecraftWaitBonusUsed);
	native->afterNewRound(false, true);
	ASSERT_TRUE(forecast::Transitions::advanceRound(state, false, nullptr));
	const auto & current = state.unit(incoming->unitId())->activation;
	EXPECT_EQ(current.moved, native->movedThisRound);
	EXPECT_EQ(current.waited, native->waitedThisTurn);
	EXPECT_EQ(current.waiting, native->waiting);
	EXPECT_EQ(current.defending, native->defending);
	EXPECT_EQ(current.hadMorale, native->hadMorale);
	EXPECT_EQ(current.waitBonusUsed, native->battlecraftWaitBonusUsed);
	EXPECT_EQ(current.retaliation.available, native->counterAttacks.available());
}

TEST_F(ForecastStateTest, BranchDiagnosticDeltasNeverInheritParentWork)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto parentState = root();
	const auto id = incoming->unitId();
	ASSERT_TRUE(forecast::Transitions::applyDamage(parentState, id, 3, battle::DamageProvenance::OTHER, false, nullptr));
	auto branch = parentState.fork();
	ASSERT_TRUE(forecast::Transitions::applyDamage(branch, id, 5, battle::DamageProvenance::OTHER, false, nullptr));
	EXPECT_EQ(branch.unit(id)->health.available(), parentState.unit(id)->health.available() - 5);
#ifdef NH_PERF_EXPERIMENTS
	EXPECT_EQ(parentState.work().detachedHealthUpdates, 1u);
	EXPECT_EQ(branch.work().detachedHealthUpdates, 1u);
	forecast::Counters aggregate;
	aggregate.accumulate(parentState.work()); aggregate.accumulate(branch.work());
	EXPECT_EQ(aggregate.detachedHealthUpdates, 2u);
	aggregate.add(&forecast::Counters::compactFactHits, std::numeric_limits<uint64_t>::max());
	aggregate.add(&forecast::Counters::compactFactHits);
	EXPECT_TRUE(aggregate.saturated);
	EXPECT_EQ(aggregate.compactFactHits, std::numeric_limits<uint64_t>::max());
#endif
}
