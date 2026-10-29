/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/NewHorizonsDreadCavalryFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/DreadCavalryProjection.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/battle/NewHorizonsOpportunist.h"
namespace
{
class DreadEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit DreadEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
	std::shared_ptr<HypotheticBattle> model(CBattleInfoCallback * source)
	{
		return std::make_shared<HypotheticBattle>(this,
			std::shared_ptr<CBattleInfoCallback>(source, [](CBattleInfoCallback *) {}));
	}
};
template<typename T> auto bytes(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}
class NewHorizonsDreadCavalryAITest : public NewHorizonsDreadCavalryFixture
{
protected:
	AttackPossibility pendingPreview(const std::shared_ptr<HypotheticBattle> & model, DamageCache & cache)
	{
		return AttackPossibility::evaluate(BattleAttackInfo(model->battleGetUnitByID(source->unitId()),
			model->battleGetUnitByID(other->unitId()), 2, false), BattleHex(8, 5), cache, model);
	}
};
}
TEST_F(NewHorizonsDreadCavalryAITest, PendingDecisionChoosesOnlyLegalMeleeWithoutMutatingLiveStateOrRng)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	DreadEnvironment environment(gameState());
	const auto before = source->save();
	const auto serverRng = bytes(*gameHandler->randomizer);
	const auto gameRng = bytes(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto defaultRng = bytes(CRandomGenerator::getDefault());
	const auto callback = std::shared_ptr<CBattleInfoCallback>(battle(), [](CBattleInfoCallback *) {});
	const auto action = dreadCavalryAI::chooseContinuation(&environment, callback, source);
	EXPECT_EQ(action.actionType, EActionType::WALK_AND_ATTACK);
	EXPECT_EQ(action.stackNumber, source->unitId());
	EXPECT_EQ(source->save(), before);
	EXPECT_EQ(bytes(*gameHandler->randomizer), serverRng);
	EXPECT_EQ(bytes(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
	EXPECT_EQ(bytes(CRandomGenerator::getDefault()), defaultRng);
}
TEST_F(NewHorizonsDreadCavalryAITest, PendingPreviewCapturesFiftyExactlyOnceForEveryOwnHitAndKeepsParentPrivate)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
	DreadEnvironment environment(gameState());
	auto model = environment.model(battle());
	auto sibling = std::make_shared<HypotheticBattle>(&environment, model);
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto ap = pendingPreview(model, cache);
	EXPECT_EQ(ap.attack.dreadCavalryDamagePercent, 50);
	int own = 0;
	for(const auto & strike : ap.fortuneStrikes)
		if(strike.attackerId == source->unitId() && !strike.retaliation)
		{
			++own;
			EXPECT_EQ(strike.dreadCavalryDamagePercent, 50);
		}
	EXPECT_EQ(own, 2);
	EXPECT_EQ(model->battleGetDirectActivationOutputPercent(ap.attack),
		model->battleGetActivationOutputPercent(ap.attack.attacker) * 50 / 100);
	EXPECT_FALSE(ap.attackerState->dreadCavalryState.pending);
	EXPECT_TRUE(ap.attackerState->dreadCavalryState.usedThisActivation);
	EXPECT_TRUE(model->getForUpdate(source->unitId())->dreadCavalryState.pending);
	EXPECT_TRUE(sibling->getForUpdate(source->unitId())->dreadCavalryState.pending);
	EXPECT_TRUE(source->dreadCavalryState.pending);
}
TEST_F(NewHorizonsDreadCavalryAITest, ExplicitAdmittedFactorSurvivesRetiredPendingStateAndDoesNotLeakToIndependentAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	DreadEnvironment environment(gameState());
	auto model = environment.model(battle());
	auto actor = model->getForUpdate(source->unitId());
	auto target = model->getForUpdate(other->unitId());
	BattleAttackInfo admitted(actor.get(), target.get(), 2, false, 50);
	const auto context = dreadCavalryAI::begin(*model, admitted, BattleHex(8, 5));
	ASSERT_TRUE(context.before.pending);
	actor->dreadCavalryState.retirePending(); // Characterize the state retirement seam; not a native Rebirth simulation.
	ASSERT_TRUE(newHorizonsDreadCavalry::acceptedBlowStillLegal(*model, actor.get(), target.get(), actor->unitId(), context.before));
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto continued = AttackPossibility::evaluate(admitted, BattleHex(8, 5), cache, model, false, std::nullopt, &context);
	EXPECT_EQ(continued.attack.dreadCavalryDamagePercent, 50);
	for(const auto & strike : continued.fortuneStrikes)
		if(strike.attackerId == actor->unitId() && !strike.retaliation) EXPECT_EQ(strike.dreadCavalryDamagePercent, 50);
	const auto independent = AttackPossibility::evaluate(BattleAttackInfo(actor.get(), target.get(), 2, false),
		BattleHex(8, 5), cache, model);
	EXPECT_EQ(independent.attack.dreadCavalryDamagePercent, 100);
	EXPECT_EQ(admitted.reverse().dreadCavalryDamagePercent, 100);
}
TEST_F(NewHorizonsDreadCavalryAITest, CurrentCacheBypassesOnlyPendingOrUsedActorAndPreservesOriginalBaseline)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	DreadEnvironment environment(gameState());
	auto baseline = environment.model(battle());
	baseline->getForUpdate(source->unitId())->dreadCavalryState = {};
	DamageCache original;
	original.buildDamageCache(baseline, BattleSide::ATTACKER);
	auto model = std::make_shared<HypotheticBattle>(&environment, baseline);
	auto actor = model->getForUpdate(source->unitId());
	auto target = model->getForUpdate(other->unitId());
	actor->dreadCavalryState = source->dreadCavalryState;
	const auto half = model->battleExpectedLuckDamage(BattleAttackInfo(actor.get(), target.get(), 0, false));
	EXPECT_EQ(original.getDamage(actor.get(), target.get(), model), half);
	actor->dreadCavalryState.retirePending();
	const auto full = model->battleExpectedLuckDamage(BattleAttackInfo(actor.get(), target.get(), 0, false));
	EXPECT_GT(full, half);
	EXPECT_EQ(original.getDamage(actor.get(), target.get(), model), full);
	EXPECT_FALSE(dreadCavalryAI::hasActivationState(target.get()));
}
TEST_F(NewHorizonsDreadCavalryAITest, OriginalVictimIdentityRemainsForbiddenAfterResurrection)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	DreadEnvironment environment(gameState());
	auto model = environment.model(battle());
	auto resurrected = model->getForUpdate(victim->unitId());
	int64_t healing = victim->getMaxHealth();
	resurrected->heal(healing, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	ASSERT_TRUE(resurrected->alive());
	EXPECT_FALSE(newHorizonsDreadCavalry::permitsTarget(*model,
		model->battleGetUnitByID(source->unitId()), resurrected.get()));
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	PotentialTargets targets(model->battleGetUnitByID(source->unitId()), cache, model);
	EXPECT_FALSE(std::ranges::any_of(targets.possibleAttacks, [&](const auto & ap)
		{ return ap.attack.defender->unitId() == victim->unitId(); }));
}
TEST_F(NewHorizonsDreadCavalryAITest, NewOrdinaryKillArmsOneContinuationUsingCommittedMovementCost)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	DreadEnvironment environment(gameState());
	auto model = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto ap = AttackPossibility::evaluate(BattleAttackInfo(model->battleGetUnitByID(source->unitId()),
		model->battleGetUnitByID(victim->unitId()), 1, false), BattleHex(6, 5), cache, model);
	ASSERT_TRUE(ap.attackerState);
	EXPECT_TRUE(ap.attackerState->dreadCavalryState.pending);
	EXPECT_EQ(ap.attackerState->dreadCavalryState.victimId, victim->unitId());
	EXPECT_EQ(ap.attackerState->dreadCavalryState.remainingMovement, source->getMovementRange(0) - 1);
	EXPECT_EQ(ap.attack.dreadCavalryDamagePercent, 100);
	EXPECT_FALSE(source->dreadCavalryState.pending);
}
TEST_F(NewHorizonsDreadCavalryAITest, SelectedReplayRetiresPendingAndCopiesUsedWithoutNewActivation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	DreadEnvironment environment(gameState());
	auto model = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto ap = pendingPreview(model, cache);
	auto selected = std::make_shared<HypotheticBattle>(&environment, model);
	const auto serial = selected->getBattle()->getActivationSerial();
	BattleExchangeVariant exchange;
	exchange.trackAttack(ap, selected, cache);
	const auto actor = selected->getForUpdate(source->unitId());
	EXPECT_FALSE(actor->dreadCavalryState.pending);
	EXPECT_TRUE(actor->dreadCavalryState.usedThisActivation);
	EXPECT_EQ(selected->getForUpdate(other->unitId())->getAvailableHealth(),
		ap.effectPreview->getForUpdate(other->unitId())->getAvailableHealth());
	EXPECT_EQ(selected->getBattle()->getActivationSerial(), serial);
	EXPECT_TRUE(model->getForUpdate(source->unitId())->dreadCavalryState.pending);
}
TEST_F(NewHorizonsDreadCavalryAITest, FutureScalarCompletesFullAdmittedMultiattackOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
	DreadEnvironment environment(gameState());
	auto model = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	auto actor = model->getForUpdate(source->unitId());
	auto target = model->getForUpdate(other->unitId());
	const auto expected = model->battleExpectedLuckDamage(BattleAttackInfo(actor.get(), target.get(), 0, false, 50));
	const auto health = target->getAvailableHealth();
	BattleExchangeVariant exchange;
	BattleExchangeAttackResult result;
	exchange.trackAttack(actor, target, false, true, cache, model, false, true,
		nullptr, nullptr, nullptr, &result);
	EXPECT_TRUE(result.completedAction);
	EXPECT_EQ(target->getAvailableHealth(), health - 2 * expected);
	EXPECT_FALSE(actor->dreadCavalryState.pending);
	EXPECT_TRUE(actor->dreadCavalryState.usedThisActivation);
}
TEST_F(NewHorizonsDreadCavalryAITest, DeclineWhenNoTargetPreservesLegacyTailAndDoesNotDefend)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	DreadEnvironment environment(gameState());
	auto model = environment.model(battle());
	auto actor = model->getForUpdate(source->unitId());
	actor->pursuitMovementRemaining = 3;
	auto target = model->getForUpdate(other->unitId());
	int64_t lethal = target->getAvailableHealth();
	target->damage(lethal);
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	EXPECT_EQ(dreadCavalryAI::projectPending(model, actor->unitId(), cache).value, 0);
	EXPECT_FALSE(actor->dreadCavalryState.pending);
	EXPECT_TRUE(actor->dreadCavalryState.usedThisActivation);
	EXPECT_EQ(actor->pursuitMovementRemaining, 3);
	EXPECT_FALSE(actor->defended());
}
TEST_F(NewHorizonsDreadCavalryAITest, HistoricalCaptureKeepsHundredPercentAndNoActivationState)
{
	captured = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	EXPECT_FALSE(source->dreadCavalryState.pending);
	DreadEnvironment environment(gameState());
	auto model = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto ap = pendingPreview(model, cache);
	EXPECT_EQ(ap.attack.dreadCavalryDamagePercent, 100);
	EXPECT_FALSE(dreadCavalryAI::hasActivationState(ap.attackerState.get()));
}

TEST_F(NewHorizonsDreadCavalryAITest, ZeroRemainingMovementStillAdmitsAdjacentZeroCostAttack)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	const auto * adjacent = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(6, 4), 1000);
	ASSERT_TRUE(battle()->isMeleeAttackPossible(source, adjacent));
	DreadEnvironment environment(gameState());
	auto model = environment.model(battle());
	auto actor = model->getForUpdate(source->unitId());
	actor->dreadCavalryState.remainingMovement = 0;
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	PotentialTargets targets(actor.get(), cache, model);
	ASSERT_FALSE(targets.possibleAttacks.empty());
	EXPECT_TRUE(std::ranges::any_of(targets.possibleAttacks, [&](const auto & ap)
		{ return ap.from == source->getPosition() && ap.attack.defender->unitId() == adjacent->unitId()
			&& !ap.attack.shooting && ap.attack.dreadCavalryDamagePercent == 50; }));
	for(const auto & ap : targets.possibleAttacks) EXPECT_EQ(ap.from, source->getPosition());
}
TEST_F(NewHorizonsDreadCavalryAITest, ContinuationDoesNotResetActivationButNextRealTurnDoes)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	DreadEnvironment environment(gameState());
	auto model = environment.model(battle());
	const auto before = model->getForUpdate(source->unitId())->save();
	model->nextTurn(source->unitId(), BattleUnitTurnReason::DREAD_CAVALRY_CONTINUATION);
	EXPECT_EQ(model->getForUpdate(source->unitId())->save(), before);
	EXPECT_TRUE(model->getForUpdate(source->unitId())->dreadCavalryState.pending);
	EXPECT_TRUE(model->getForUpdate(source->unitId())->dreadCavalryState.usedThisActivation);
	model->nextTurn(source->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(dreadCavalryAI::hasActivationState(model->getForUpdate(source->unitId()).get()));
	EXPECT_EQ(model->battleGetDreadCavalryDamagePercent(model->battleGetUnitByID(source->unitId())), 100);
}

TEST_F(NewHorizonsDreadCavalryAITest, OrdinaryOtherActorBoundaryRetiresOnlyAbandonedPrivatePending)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	DreadEnvironment environment(gameState());
	auto model = environment.model(battle());
	model->nextTurn(other->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	const auto abandoned = model->getForUpdate(source->unitId());
	EXPECT_FALSE(abandoned->dreadCavalryState.pending);
	EXPECT_TRUE(abandoned->dreadCavalryState.usedThisActivation);
	EXPECT_TRUE(source->dreadCavalryState.pending);
}

TEST_F(NewHorizonsDreadCavalryAITest, SelectedContinuationRestoresConsumedPursuitTailWithoutOpportunist)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(killFirst());
	DreadEnvironment environment(gameState());
	auto model = environment.model(battle());
	auto actor = model->getForUpdate(source->unitId());
	ASSERT_FALSE(newHorizonsOpportunist::hasPerk(*model, actor.get()));
	ASSERT_TRUE(actor->dreadCavalryState.pending);
	actor->dreadCavalryState.remainingMovement = 5;
	actor->dreadCavalryState.sharedTailCap = 5;
	actor->dreadCavalryState.independentVanish = 0;
	actor->pursuitMovementRemaining = 5;
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto prediction = pendingPreview(model, cache);
	ASSERT_TRUE(prediction.attackerState);
	EXPECT_FALSE(prediction.attackerState->dreadCavalryState.pending);
	EXPECT_TRUE(prediction.attackerState->dreadCavalryState.usedThisActivation);
	ASSERT_EQ(prediction.attackerState->pursuitMovementRemaining, 3);
	auto selected = std::make_shared<HypotheticBattle>(&environment, model);
	BattleExchangeVariant replay;
	replay.trackAttack(prediction, selected, cache);
	const auto replayed = selected->getForUpdate(source->unitId());
	EXPECT_FALSE(replayed->dreadCavalryState.pending);
	EXPECT_EQ(replayed->pursuitMovementRemaining, 3);
	EXPECT_EQ(actor->pursuitMovementRemaining, 5);
	EXPECT_TRUE(source->dreadCavalryState.pending);
}
