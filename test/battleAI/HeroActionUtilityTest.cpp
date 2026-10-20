/*
 * HeroActionUtilityTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/HeroActionUtility.h"
#include "../../AI/BattleAI/HeroActionContinuation.h"
#include "../../AI/BattleAI/HeroActionTiming.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/serializer/CMemorySerializer.h"

#include <array>
#include <bit>

namespace
{
class UtilityEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit UtilityEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

template<typename T> std::vector<std::byte> serialized(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}

uint32_t bits(float value) { return std::bit_cast<uint32_t>(value); }

// Frozen reference: exact pre-extraction continuation body, verified against
// the evaluator beforeimage by the private source check (not a second policy).
void legacyContinuation(float & stackActionScore,
	const std::shared_ptr<StackWithBonuses> & modelActive, bool needFullEval,
	const CachedAttack & cachedAttack, DamageCache & innerCache,
	const std::shared_ptr<HypotheticBattle> & state, const std::shared_ptr<Environment> & env,
	float strengthRatio, int simulationTurnsCount,
	const BattleExchangeEvaluator & scoreEvaluator, PotentialTargets * targets)
{
	if(modelActive->alive() && (needFullEval || !cachedAttack.ap))
	{
#if BATTLE_TRACE_LEVEL >= 1
		logAi->trace("Full evaluation: movement range/position changed or no cached attack.");
#endif

		PotentialTargets innerTargets(modelActive.get(), innerCache, state);
		BattleExchangeEvaluator innerEvaluator(state, env, strengthRatio, simulationTurnsCount);

		innerEvaluator.updateReachabilityMap(state);

		auto moveTarget = innerEvaluator.findMoveTowardsUnreachable(modelActive.get(), innerTargets, innerCache, state);

		if(!innerTargets.possibleAttacks.empty())
		{
			auto newStackAction = innerEvaluator.findBestTarget(modelActive.get(), innerTargets, innerCache, state);

			stackActionScore = std::max(moveTarget.score, newStackAction.score);
		}
		else
		{
			stackActionScore = moveTarget.score;
		}
	}
	else if(modelActive->alive())
	{
		auto updatedAttacker = state->getForUpdate(cachedAttack.ap->attack.attacker->unitId());
		auto updatedDefender = state->getForUpdate(cachedAttack.ap->attack.defender->unitId());
		auto updatedBai = BattleAttackInfo(
			updatedAttacker.get(),
			updatedDefender.get(),
			cachedAttack.ap->attack.chargeDistance,
			cachedAttack.ap->attack.shooting);

		auto updatedAttack = AttackPossibility::evaluate(updatedBai, cachedAttack.ap->from, innerCache, state, cachedAttack.ap->perfectMoment);

		BattleExchangeEvaluator innerEvaluator(scoreEvaluator);

		stackActionScore = innerEvaluator.evaluateExchange(updatedAttack, cachedAttack.turn, *targets, innerCache, state);
	}
}
}

TEST(HeroActionCompositionTest, AbsoluteContinuationAndImmediateEffectsEnterExactlyOnce)
{
	const float noCastBaseline = 909.6513671875f;
	const heroActionUtility::AbsoluteContinuationUtility after{2281.921875f};
	const heroActionUtility::ImmediateSignedUtility immediate{2625.0f, 0.0f, 0.0f};
	const auto result = heroActionUtility::composeAbsoluteUtility(after, immediate);
	EXPECT_EQ(bits(result.value), bits(4906.921875f));
	EXPECT_NE(bits(result.value), bits(noCastBaseline + after.value + immediate.toHostiles));
	EXPECT_NE(bits(result.value), bits(after.value + 2 * immediate.toHostiles));
}

TEST(HeroActionCompositionTest, PreservesLeftAssociationAndIneffectiveSentinelBranch)
{
	const std::array<float, 4> exchanges{0.0f, 16777216.0f, -23.5f,
		static_cast<float>(EvaluationResult::INEFFECTIVE_SCORE)};
	const std::array<heroActionUtility::ImmediateSignedUtility, 3> effects{{
		{-16777216.0f, 1.0f, 0.0f}, {17.25f, -4.5f, 0.25f}, {-0.0f, -0.0f, -0.0f}}};
	for(const auto exchange : exchanges)
		for(const auto & effect : effects)
		{
			const float legacy = vstd::isAlmostEqual(exchange, static_cast<float>(EvaluationResult::INEFFECTIVE_SCORE))
				? effect.toFriendlies + effect.toHostiles + effect.initiative
				: exchange + effect.toFriendlies + effect.toHostiles + effect.initiative;
			EXPECT_EQ(bits(heroActionUtility::composeAbsoluteUtility({exchange}, effect).value), bits(legacy));
		}
	EXPECT_EQ(heroActionUtility::composeAbsoluteUtility({16777216.0f}, effects.front()).value, 0.0f);
}

class HeroActionUtilityTest : public HeroCommandFixture
{
protected:
	CStack * own = nullptr;
	CStack * low = nullptr;
	CStack * high = nullptr;
	std::shared_ptr<UtilityEnvironment> environment;
	std::shared_ptr<CBattleInfoCallback> live;
	std::shared_ptr<HypotheticBattle> model;
	DamageCache cache;
	std::unique_ptr<BattleExchangeEvaluator> evaluator;
	PossibleSpellcast candidate;

	void resetModel(float strength = 1.0f)
	{
		model = std::make_shared<HypotheticBattle>(environment.get(), live);
		cache = DamageCache();
		cache.buildDamageCache(model, BattleSide::ATTACKER);
		evaluator = std::make_unique<BattleExchangeEvaluator>(model, environment, strength, 2);
	}

	void prepare()
	{
		startGame();
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		own = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 10);
		low = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(71), 10);
		high = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(75), 10);
		high->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::CREATURE_DAMAGE, BonusSource::OTHER, 1000, BonusSourceID(), BonusCustomSubtype::creatureDamageBoth));
		beginCombat();
		battle()->activeStack = own->unitId();
		environment = std::make_shared<UtilityEnvironment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		resetModel();
	}

	std::optional<heroActionUtility::HealthDeltaUtility> value(CStack * original,
		heroActionUtility::ImmediateSignedUtility & immediate, bool phantom = false)
	{
		const auto * projected = model->battleGetUnitByID(original->unitId());
		return heroActionUtility::accumulateHealthDelta(
			{nullptr, original, projected, original->getAvailableHealth(), projected->getAvailableHealth(), phantom},
			candidate, PlayerColor(0), cache, model, *evaluator, immediate);
	}
};

TEST_F(HeroActionUtilityTest, HostileHpDeltaRetainsVictimIdentityAndExistingBountyBits)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const int64_t before = low->getAvailableHealth();
	int64_t loss = 100;
	model->getForUpdate(low->unitId())->damage(loss);
	const auto expected = AttackPossibility::calculateDamageReduce(nullptr, low, loss, cache, model);
	heroActionUtility::ImmediateSignedUtility immediate{17.25f, -3.0f, 0.5f};
	const auto result = value(low, immediate);
	ASSERT_TRUE(result);
	EXPECT_EQ(bits(result->dpsReduce), bits(expected));
	EXPECT_EQ(bits(immediate.toHostiles), bits(17.25f + expected * evaluator->getPositiveEffectMultiplier()));
	EXPECT_EQ(immediate.toFriendlies, -3.0f);
	EXPECT_EQ(low->getAvailableHealth(), before);
}

TEST_F(HeroActionUtilityTest, EqualHpLossAgainstDifferentOutputVictimsIsNotRawHpUtility)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(low->getAvailableHealth(), high->getAvailableHealth());
	std::array<float, 2> values;
	std::array<CStack *, 2> victims{low, high};
	for(size_t index = 0; index < victims.size(); ++index)
	{
		auto * victim = victims[index];
		int64_t loss = victim->getAvailableHealth();
		model->getForUpdate(victim->unitId())->damage(loss);
		const auto capped = AttackPossibility::calculateDamageReduce(nullptr, victim,
			100 * victim->getAvailableHealth(), cache, model);
		heroActionUtility::ImmediateSignedUtility immediate{};
		const auto result = value(victim, immediate);
		ASSERT_TRUE(result);
		EXPECT_EQ(bits(result->dpsReduce), bits(capped)) << "Full kill equals capped overkill, not raw HP";
		EXPECT_EQ(bits(immediate.toHostiles), bits(result->dpsReduce));
		values[index] = immediate.toHostiles;
	}
	EXPECT_GT(values[1], values[0]);
	EXPECT_GT(values[0], 0.0f);
}

TEST_F(HeroActionUtilityTest, CollateralKeepsFourfoldPenaltyAndLazyNegativeWeight)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	resetModel(0.5f);
	int64_t loss = 100;
	model->getForUpdate(own->unitId())->damage(loss);
	const auto expected = AttackPossibility::calculateDamageReduce(nullptr, own, loss, cache, model);
	heroActionUtility::ImmediateSignedUtility immediate{7.0f, -3.0f, 0.0f};
	const auto result = value(own, immediate);
	ASSERT_TRUE(result);
	EXPECT_EQ(bits(immediate.toFriendlies), bits(-3.0f - 4 * expected * evaluator->getNegativeEffectMultiplier()));
	EXPECT_EQ(immediate.toHostiles, 7.0f);
}

TEST_F(HeroActionUtilityTest, StormResistanceScalesHostileDamageOnlyAtExistingStage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	low->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 25, BonusSourceID()));
	resetModel();
	candidate.spellStormOfDaggers = true;
	int64_t loss = 100;
	model->getForUpdate(low->unitId())->damage(loss);
	auto expected = AttackPossibility::calculateDamageReduce(nullptr, low, loss, cache, model);
	expected *= 1.0f - static_cast<float>(std::clamp(low->magicResistance(), 0, 100)) / 100.0f;
	heroActionUtility::ImmediateSignedUtility immediate{};
	const auto result = value(low, immediate);
	ASSERT_TRUE(result);
	EXPECT_EQ(bits(result->dpsReduce), bits(expected));
	EXPECT_EQ(bits(immediate.toHostiles), bits(expected));
}

TEST_F(HeroActionUtilityTest, MagicalFriendlyHealingPreservesEarlyContinueNotZeroDamageReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	int64_t wound = 100;
	own->damage(wound);
	resetModel();
	auto healed = model->getForUpdate(own->unitId());
	int64_t healing = 100;
	healed->heal(healing, EHealLevel::HEAL, EHealPower::PERMANENT);
	healed->summoned = true;
	ASSERT_GT(healed->getAvailableHealth(), own->getAvailableHealth());
	heroActionUtility::ImmediateSignedUtility immediate{7.0f, -3.0f, 0.5f};
	EXPECT_FALSE(value(own, immediate));
	EXPECT_EQ(immediate.toHostiles, 7.0f);
	EXPECT_EQ(immediate.toFriendlies, -3.0f);
}

TEST_F(HeroActionUtilityTest, HealthValuationLeavesLiveHealthLedgerOrdersRngAndTrafficAlone)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto original = low->acquireState()->save().toString();
	const auto ledger = serialized(battle()->getHeroActionAllowances(BattleSide::ATTACKER));
	const auto orders = serialized(battle()->getHeroOrderStates(BattleSide::ATTACKER));
	const auto rng = serialized(*gameHandler->randomizer);
	const auto traffic = server.startedActions.size();
	int64_t loss = 100;
	model->getForUpdate(low->unitId())->damage(loss);
	for(int repeat = 0; repeat < 3; ++repeat)
	{
		heroActionUtility::ImmediateSignedUtility immediate{};
		ASSERT_TRUE(value(low, immediate));
	}
	EXPECT_EQ(low->acquireState()->save().toString(), original);
	EXPECT_EQ(serialized(battle()->getHeroActionAllowances(BattleSide::ATTACKER)), ledger);
	EXPECT_EQ(serialized(battle()->getHeroOrderStates(BattleSide::ATTACKER)), orders);
	EXPECT_EQ(serialized(*gameHandler->randomizer), rng);
	EXPECT_EQ(server.startedActions.size(), traffic);
}

TEST_F(HeroActionUtilityTest, DeadContinuationKeepsPriorZeroWithoutTouchingCachedTarget)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto active = model->getForUpdate(own->unitId());
	int64_t lethal = active->getAvailableHealth();
	active->damage(lethal);
	ASSERT_FALSE(active->alive());
	CachedAttack empty;
	heroActionUtility::AbsoluteContinuationUtility continuation{0};
	heroActionTiming::Continuation timing;
	heroActionUtility::evaluateContinuation(continuation, active, false, empty, cache, model,
		environment, 1.0f, 2, *evaluator, nullptr, &timing);
	EXPECT_EQ(continuation.value, 0.0f);
	EXPECT_EQ(timing.full.calls, 0u);
	EXPECT_EQ(timing.cached.calls, 0u);
	EXPECT_EQ(timing.fullSetup.calls, 0u);
	EXPECT_EQ(timing.fullReachability.calls, 0u);
	EXPECT_EQ(timing.fullMovement.calls, 0u);
	EXPECT_EQ(timing.fullBestTarget.calls, 0u);
}

TEST_F(HeroActionUtilityTest, FullAndCachedContinuationMatchFrozenLegacyBodyBitForBit)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto rng = serialized(*gameHandler->randomizer);
	const auto traffic = server.startedActions.size();
	const std::array liveStates{own->acquireState()->save().toString(),
		low->acquireState()->save().toString(), high->acquireState()->save().toString()};
	for(const bool timed : {false, true})
	for(const bool cached : {false, true})
	{
		auto reference = std::make_shared<HypotheticBattle>(environment.get(), live);
		auto extracted = std::make_shared<HypotheticBattle>(environment.get(), live);
		DamageCache referenceCache, extractedCache;
		referenceCache.buildDamageCache(reference, BattleSide::ATTACKER);
		extractedCache.buildDamageCache(extracted, BattleSide::ATTACKER);
		const auto referenceActive = reference->getForUpdate(own->unitId());
		const auto extractedActive = extracted->getForUpdate(own->unitId());
		PotentialTargets referenceTargets(referenceActive.get(), referenceCache, reference);
		PotentialTargets extractedTargets(extractedActive.get(), extractedCache, extracted);
		ASSERT_FALSE(referenceTargets.possibleAttacks.empty());
		ASSERT_FALSE(extractedTargets.possibleAttacks.empty());
		CachedAttack referenceAttack, extractedAttack;
		if(cached)
		{
			referenceAttack.ap = referenceTargets.bestAction();
			extractedAttack.ap = extractedTargets.bestAction();
			referenceAttack.turn = extractedAttack.turn = 0;
		}
		BattleExchangeEvaluator referenceEvaluator(reference, environment, 0.5f, 2);
		BattleExchangeEvaluator extractedEvaluator(extracted, environment, 0.5f, 2);
		float expected = 0;
		heroActionTiming::Continuation timing;
		legacyContinuation(expected, referenceActive, !cached, referenceAttack, referenceCache,
			reference, environment, 0.5f, 2, referenceEvaluator, &referenceTargets);
		heroActionUtility::AbsoluteContinuationUtility actual{0};
		heroActionUtility::evaluateContinuation(actual, extractedActive, !cached, extractedAttack,
			extractedCache, extracted, environment, 0.5f, 2, extractedEvaluator, &extractedTargets,
			timed ? &timing : nullptr);
		EXPECT_EQ(timing.full.calls, timed && !cached ? 1u : 0u);
		EXPECT_EQ(timing.cached.calls, timed && cached ? 1u : 0u);
		EXPECT_EQ(timing.fullSetup.calls, timed && !cached ? 1u : 0u);
		EXPECT_EQ(timing.fullReachability.calls, timed && !cached ? 1u : 0u);
		EXPECT_EQ(timing.fullMovement.calls, timed && !cached ? 1u : 0u);
		EXPECT_EQ(timing.fullBestTarget.calls, timed && !cached ? 1u : 0u);
		const auto & details = timing.fullBestTargetDetails;
		for(const auto * exchange : {&details.waitedDetails, &details.normalDetails})
		{
			if(timed && !cached)
			{
				EXPECT_GT(exchange->body.calls, 0u);
				EXPECT_EQ(exchange->body.totalMicroseconds, exchange->membership.totalMicroseconds
					+ exchange->projectionSetup.totalMicroseconds + exchange->targetProbes.totalMicroseconds
					+ exchange->appliedAttacks.totalMicroseconds + exchange->activationStart.totalMicroseconds
					+ exchange->activationEnd.totalMicroseconds + exchange->roundAdvance.totalMicroseconds
					+ exchange->remainderMicroseconds);
				EXPECT_LE(exchange->targetProbePreparation.totalMicroseconds
					+ exchange->targetProbeMainDamage.totalMicroseconds + exchange->targetProbeDpsScore.totalMicroseconds,
					exchange->targetProbes.totalMicroseconds);
			}
			else
				EXPECT_EQ(exchange->body.calls, 0u);
			EXPECT_FALSE(exchange->collectProbeDamageCache);
		}

		EXPECT_EQ(bits(actual.value), bits(expected));
		for(const auto * original : {own, low, high})
			EXPECT_EQ(reference->battleGetUnitByID(original->unitId())->acquireState()->save().toString(),
				extracted->battleGetUnitByID(original->unitId())->acquireState()->save().toString());
	}
	EXPECT_EQ(serialized(*gameHandler->randomizer), rng);
	EXPECT_EQ(server.startedActions.size(), traffic);
	EXPECT_EQ((std::array{own->acquireState()->save().toString(),
		low->acquireState()->save().toString(), high->acquireState()->save().toString()}), liveStates);
}
