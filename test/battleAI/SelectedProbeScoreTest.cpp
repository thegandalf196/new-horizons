/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../lib/battle/DamageCalculationTiming.h"
#include "../../lib/battle/NewHorizonsShroud.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/CRandomGenerator.h"
#include <bit>
#include <chrono>

namespace
{
class UnknownScoreUpdater final : public IUpdater {};

JsonNode decision(const EvaluationResult & result)
{
	JsonNode value;
	value["score"].Integer() = std::bit_cast<uint32_t>(result.score);
	value["wait"].Bool() = result.wait;
	value["defend"].Bool() = result.defend;
	value["actor"].Integer() = result.bestAttack.attack.attacker->unitId();
	value["target"].Integer() = result.bestAttack.attack.defender->unitId();
	value["shooting"].Bool() = result.bestAttack.attack.shooting;
	value["from"].Integer() = result.bestAttack.from.toInt();
	value["destination"].Integer() = result.bestAttack.dest.toInt();
	value["moveScore"].Integer() = std::bit_cast<uint32_t>(result.bestMove.score);
	value["moveTurns"].Integer() = result.bestMove.turnsToReach;
	for(const auto hex : result.bestMove.positions)
	{
		JsonNode position;
		position.Integer() = hex.toInt();
		value["positions"].Vector().push_back(std::move(position));
	}
	if(result.bestAttack.attackerState)
		value["actorState"] = result.bestAttack.attackerState->save();
	for(const auto & unit : result.bestAttack.affectedUnits)
		value["affectedUnits"].Vector().push_back(unit->save());
	return value;
}

class SelectedProbeScoreTest : public HeroCommandFixture
{
protected:
	std::shared_ptr<projectedHitTest::Environment> environment;
	std::shared_ptr<HypotheticBattle> model;
	std::vector<uint32_t> ids;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
		useRevisedOrders = true;
	}

	void prepare(bool capacity, bool extraHit, bool laterWinner, bool unknownSource = false)
	{
		startGame();
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			for(int row = 0; row < 2; ++row)
				ids.push_back(addStack(side, creatureByName("core:pikeman"),
					BattleHex(side == BattleSide::ATTACKER ? 3 : 12, 3 + row * 2), 10)->unitId());
		beginCombat();
		environment = std::make_shared<projectedHitTest::Environment>(gameState());
		auto live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		model = std::make_shared<HypotheticBattle>(environment.get(), live);
		model->getForUpdate(ids[1])->position = BattleHex(4, 2);
		model->getForUpdate(ids[2])->position = BattleHex(4, 3);
		model->getForUpdate(ids[3])->position = BattleHex(3, 4);
		for(const auto & stack : battle()->stacks) (void)stack->counterAttacks.available();
		for(const auto id : ids)
		{
			auto unit = model->getForUpdate(id);
			(void)unit->counterAttacks.available();
			unit->addUnitBonus({
				Bonus(BonusDuration::ONE_BATTLE, BonusType::BLOCKS_RETALIATION, BonusSource::OTHER, 1, BonusSourceID()),
				Bonus(BonusDuration::ONE_BATTLE, BonusType::NO_LUCK, BonusSource::OTHER, 1, BonusSourceID())});
		}
		for(const auto [index, speed] : {std::pair{0, 30}, std::pair{2, 20}, std::pair{1, 10}})
			model->getForUpdate(ids[index])->addUnitBonus({
				Bonus(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED, BonusSource::OTHER, speed, BonusSourceID())});
		if(laterWinner)
			model->getForUpdate(ids[1])->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE,
				BonusType::GENERAL_DAMAGE_REDUCTION, BonusSource::OTHER, 80, BonusSourceID(),
				BonusSubtypeID(BonusCustomSubtype::damageTypeAll))});
		if(extraHit)
		{
			auto actor = model->getForUpdate(ids[2]);
			actor->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE, BonusType::ADDITIONAL_ATTACK,
				BonusSource::OTHER, 1, BonusSourceID())});
			actor->addUnitBonus(newHorizonsShroud::nightProwlerDamageBonuses());
		}
		if(capacity)
			for(const auto id : {ids[0], ids[1]})
			{
				auto unit = model->getForUpdate(id);
				unit->preserveCreatureHealthOnCapacityIncrease();
				unit->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE, BonusType::STACK_HEALTH,
					BonusSource::HERO_SPECIAL, 5, BonusSourceID())});
				unit->normalizeCapacityHealth();
				ASSERT_TRUE(unit->health.isCapacityHealthTracking());
			}
		if(unknownSource)
		{
			Bonus harmless(BonusDuration::ONE_BATTLE, BonusType::NONE, BonusSource::OTHER, 0, BonusSourceID());
			harmless.updater = std::make_shared<UnknownScoreUpdater>();
			model->getForUpdate(ids[0])->addUnitBonus({harmless});
		}
	}

	std::vector<JsonNode> states() const
	{
		std::vector<JsonNode> values;
		for(const auto id : ids) values.push_back(model->getForUpdate(id)->save());
		return values;
	}

	void compare(bool preparedParent = false, bool capacity = false, bool extraHit = false,
		bool laterWinner = false, bool unknownSource = false)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(capacity, extraHit, laterWinner, unknownSource));
		const auto world = gameState()->saveToMemory();
		const auto native = projectedHitTest::serialized(*battle());
		const auto projected = states();
		const auto serverRng = projectedHitTest::serialized(*gameHandler->randomizer);
		const auto gameRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
		const auto aiRng = projectedHitTest::serialized(CRandomGenerator::getDefault());
		DamageCache parent;
		if(preparedParent) parent.buildDamageCache(model, BattleSide::ATTACKER);
		const auto parentBefore = parent.captureSnapshot();
		std::optional<DamageCache::Snapshot> expectedCache;
		std::optional<JsonNode> expectedDecision;
		uint32_t expectedScore = 0;
		uint64_t baselineCalculations = 0;
		BattleMovementTiming::ExchangeTiming baselineTiming;
		for(const bool reuse : {false, true})
		{
			DamageCache damage(preparedParent ? &parent : nullptr);
			damage.buildCreaturePressureCache(model, BattleSide::ATTACKER);
			const auto actor = model->getForUpdate(ids[0]);
			PotentialTargets targets(actor.get(), damage, model);
			const auto attack = AttackPossibility::evaluate(
				BattleAttackInfo(actor.get(), model->battleGetUnitByID(ids[3]), 0, false),
				actor->getPosition(), damage, model);
			BattleExchangeEvaluator evaluator(model, environment, 1.0f, 2, reuse);
			evaluator.updateReachabilityMap(model);
			BattleMovementTiming::ExchangeTiming timing;
			damageCalculationTiming::Sample sample;
			float score;
			{
				damageCalculationTiming::Binding measure(&sample);
				score = evaluator.evaluateExchange(attack, 0, targets, damage, model, &timing);
			}
			const auto selected = decision(evaluator.findBestTarget(actor.get(), targets, damage, model));
			if(!reuse)
			{
				expectedScore = std::bit_cast<uint32_t>(score);
				expectedDecision = selected;
				expectedCache = damage.captureSnapshot();
				baselineCalculations = sample.initialCalls;
				baselineTiming = timing;
				EXPECT_EQ(timing.selectedProbeScoreUses, 0u);
			}
			else
			{
				ASSERT_GT(timing.selectedProbeDamageUses, 0u);
				if(unknownSource) EXPECT_EQ(timing.selectedProbeScoreUses, 0u);
				else EXPECT_EQ(timing.selectedProbeScoreUses, timing.selectedProbeDamageUses);
				EXPECT_EQ(timing.selectedProbeDamageUses, baselineTiming.selectedProbeDamageUses);
				EXPECT_EQ(timing.targetProbes.calls, baselineTiming.targetProbes.calls);
				EXPECT_EQ(timing.appliedAttacks.calls, baselineTiming.appliedAttacks.calls);
				EXPECT_EQ(std::bit_cast<uint32_t>(score), expectedScore);
				EXPECT_EQ(selected, *expectedDecision);
				EXPECT_TRUE(damage.matchesSnapshot(*expectedCache));
				ASSERT_GE(baselineCalculations, sample.initialCalls);
				EXPECT_EQ(baselineCalculations - sample.initialCalls,
					preparedParent ? 0u : timing.selectedProbeScoreUses);
				RecordProperty("BaselineCalculations", std::to_string(baselineCalculations));
				RecordProperty("ReusedCalculations", std::to_string(sample.initialCalls));
				RecordProperty("SelectedScoreUses", std::to_string(timing.selectedProbeScoreUses));
			}
			EXPECT_TRUE(parent.matchesSnapshot(parentBefore));
			EXPECT_EQ(gameState()->saveToMemory(), world);
			EXPECT_EQ(projectedHitTest::serialized(*battle()), native);
			EXPECT_EQ(states(), projected);
			EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverRng);
			EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
			EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), aiRng);
		}
	}

	void benchmark()
	{
		ASSERT_NO_FATAL_FAILURE(prepare(false, false, false));
		std::optional<uint32_t> expected;
		constexpr int samples = 8, repeats = 8;
		for(int sample = 0; sample < samples; ++sample)
			for(int order = 0; order < 2; ++order)
			{
				const bool reuse = (sample + order) % 2;
				DamageCache damage;
				damage.buildCreaturePressureCache(model, BattleSide::ATTACKER);
				const auto actor = model->getForUpdate(ids[0]);
				PotentialTargets targets(actor.get(), damage, model);
				const auto attack = AttackPossibility::evaluate(
					BattleAttackInfo(actor.get(), model->battleGetUnitByID(ids[3]), 0, false),
					actor->getPosition(), damage, model);
				BattleExchangeEvaluator evaluator(model, environment, 1.0f, 2, reuse);
				evaluator.updateReachabilityMap(model);
				BattleMovementTiming::ExchangeTiming timing;
				const auto warm = std::bit_cast<uint32_t>(evaluator.evaluateExchange(attack, 0, targets, damage, model, &timing));
				if(!expected) expected = warm;
				EXPECT_EQ(warm, *expected);
				EXPECT_GT(timing.selectedProbeDamageUses, 0u);
				EXPECT_EQ(timing.selectedProbeScoreUses, reuse ? timing.selectedProbeDamageUses : 0u);
				// Actual complete two-round exchange, including the full source proof
				// on every selected actor. AP/target/reachability preparation stays outside
				// this measured phase. No per-damage diagnostic clocks are installed.
				uint64_t checksum = 0;
				const auto start = std::chrono::steady_clock::now();
				for(int repeat = 0; repeat < repeats; ++repeat)
					checksum += std::bit_cast<uint32_t>(evaluator.evaluateExchange(attack, 0, targets, damage, model));
				const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - start).count();
				EXPECT_EQ(checksum, static_cast<uint64_t>(*expected) * repeats);
				RecordProperty("Sample" + std::to_string(sample) + (reuse ? "ReuseNanoseconds" : "BaselineNanoseconds"),
					std::to_string(elapsed));
			}
		RecordProperty("Workload", "Four ordinary Pikemen stacks; two rounds; eight repeats per alternating sample; warm query phase");
	}
};
}

TEST_F(SelectedProbeScoreTest, DynamicReverseDamageRemovesExactlyOneCalculationPerSelectedScore)
{
	ASSERT_NO_FATAL_FAILURE(compare());
}

TEST_F(SelectedProbeScoreTest, PreparedOriginalPairsPreserveTheirBaselineAndCacheBookkeeping)
{
	ASSERT_NO_FATAL_FAILURE(compare(true));
}

TEST_F(SelectedProbeScoreTest, CapacityLedgerAndLaterWinnerKeepScoreBitsAndSelectedAction)
{
	ASSERT_NO_FATAL_FAILURE(compare(false, true, false, true));
}

TEST_F(SelectedProbeScoreTest, ExtraHitRecomputesAfterNightProwlerIsConsumed)
{
	ASSERT_NO_FATAL_FAILURE(compare(false, false, true, true));
}

TEST_F(SelectedProbeScoreTest, UnknownUnderlyingBonusSourceKeepsOriginalScoring)
{
	ASSERT_NO_FATAL_FAILURE(compare(false, false, false, false, true));
}

TEST_F(SelectedProbeScoreTest, ReportsAlternatingCompleteExchangeTimingsWithAdmissionIncluded)
{
	ASSERT_NO_FATAL_FAILURE(benchmark());
}
