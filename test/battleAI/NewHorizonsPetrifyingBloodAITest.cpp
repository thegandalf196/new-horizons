/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/NewHorizonsPetrifyingBloodFixture.h"
#include "../server/battles/NewHorizonsBloodAerialFixture.h"
#include "../../AI/BattleAI/PetrifyingBloodValue.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/battle/StackInitializationReceipts.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/serializer/CMemorySerializer.h"
namespace
{
class BloodEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit BloodEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
	std::shared_ptr<HypotheticBattle> model(CBattleInfoCallback * source)
	{
		return std::make_shared<HypotheticBattle>(this,
			std::shared_ptr<CBattleInfoCallback>(source, [](CBattleInfoCallback *) {}));
	}
};
template<typename T> std::vector<std::byte> bytes(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}
class NewHorizonsPetrifyingBloodAITest : public NewHorizonsPetrifyingBloodBattleTest {};
}
TEST_F(NewHorizonsPetrifyingBloodAITest, ExpectedControlIsPositiveOnceWithoutGuaranteedStatusOrLiveMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	BloodEnvironment environment(gameState());
	auto parent = environment.model(battle());
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	DamageCache cache;
	cache.buildDamageCache(parent, BattleSide::DEFENDER);
	auto actor = enemy->acquireState();
	auto recipient = basilisk->acquireState();
	const auto native = basilisk->save();
	const auto serverRng = bytes(*gameHandler->randomizer);
	const auto gameRng = bytes(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto defaultRng = bytes(CRandomGenerator::getDefault());
	const float value = petrifyingBloodAI::controlValue(parent, *actor, *recipient, 1, false, true, cache);
	EXPECT_GT(value, 0);
	EXPECT_EQ(recipient->petrifyingBloodRound, battle()->getRound());
	EXPECT_FALSE(petrified(*actor));
	EXPECT_EQ(petrifyingBloodAI::controlValue(parent, *actor, *recipient, 1, false, true, cache), 0);
	EXPECT_EQ(basilisk->save(), native);
	EXPECT_EQ(parent->getForUpdate(basilisk->unitId())->petrifyingBloodRound, -1);
	EXPECT_EQ(sibling->getForUpdate(basilisk->unitId())->petrifyingBloodRound, -1);
	EXPECT_EQ(bytes(*gameHandler->randomizer), serverRng);
	EXPECT_EQ(bytes(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
	EXPECT_EQ(bytes(CRandomGenerator::getDefault()), defaultRng);
}
TEST_F(NewHorizonsPetrifyingBloodAITest, ActualAttackPreviewAndSelectedReplayCarryExpectedRiskAndRoundReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	BloodEnvironment environment(gameState());
	auto parent = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(parent, BattleSide::DEFENDER);
	const auto prediction = AttackPossibility::evaluate(BattleAttackInfo(enemy, basilisk, 0, false),
		enemy->getPosition(), cache, parent);
	EXPECT_LT(prediction.petrifyingBloodControlValue, 0);
	EXPECT_FALSE(petrified(*prediction.attackerState));
	auto selected = std::make_shared<HypotheticBattle>(&environment, parent);
	BattleExchangeVariant replay;
	replay.trackAttack(prediction, selected, cache);
	EXPECT_EQ(selected->getForUpdate(basilisk->unitId())->petrifyingBloodRound, battle()->getRound());
	EXPECT_EQ(parent->getForUpdate(basilisk->unitId())->petrifyingBloodRound, -1);
	EXPECT_EQ(basilisk->petrifyingBloodRound, -1);
}
TEST_F(NewHorizonsPetrifyingBloodAITest, AppliedFutureCallConsumesOnlyPrivateRecipientRoundReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	BloodEnvironment environment(gameState());
	auto parent = environment.model(battle());
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	DamageCache cache;
	cache.buildDamageCache(child, BattleSide::DEFENDER);
	BattleExchangeVariant forecast;
	forecast.trackAttack(child->getForUpdate(enemy->unitId()), child->getForUpdate(basilisk->unitId()),
		false, true, cache, child);
	EXPECT_EQ(child->getForUpdate(basilisk->unitId())->petrifyingBloodRound, battle()->getRound());
	EXPECT_FALSE(petrified(*child->getForUpdate(enemy->unitId())));
	EXPECT_EQ(parent->getForUpdate(basilisk->unitId())->petrifyingBloodRound, -1);
}
TEST_F(NewHorizonsPetrifyingBloodAITest, JsonCopyAndInitializationPreserveSpentRoundReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	basilisk->petrifyingBloodRound = battle()->getRound();
	auto state = basilisk->acquireState();
	EXPECT_EQ(state->petrifyingBloodRound, basilisk->petrifyingBloodRound);
	const auto saved = state->save();
	state->petrifyingBloodRound = -1;
	state->load(saved);
	EXPECT_EQ(state->petrifyingBloodRound, basilisk->petrifyingBloodRound);
	battle::StackInitializationReceipts receipt(*state);
	receipt.initialize(*state, basilisk, basilisk->getPosition());
	EXPECT_EQ(state->petrifyingBloodRound, basilisk->petrifyingBloodRound);
	auto invalid = saved;
	invalid["state"]["petrifyingBloodRound"].Integer() = -2;
	EXPECT_THROW(state->load(invalid), std::runtime_error);
	EXPECT_EQ(state->save(), saved);
	invalid["state"]["petrifyingBloodRound"].Integer() = int64_t(std::numeric_limits<int32_t>::max()) + 1;
	EXPECT_THROW(state->load(invalid), std::runtime_error);
	EXPECT_EQ(state->save(), saved);
	invalid["state"]["petrifyingBloodRound"].Float() = 1.5;
	EXPECT_THROW(state->load(invalid), std::runtime_error);
	EXPECT_EQ(state->save(), saved);
	auto partial = saved;
	partial["state"].Struct().erase("petrifyingBloodRound");
	state->load(partial);
	EXPECT_EQ(state->petrifyingBloodRound, basilisk->petrifyingBloodRound);
}

TEST_F(NewHorizonsPetrifyingBloodAITest, PostSequenceDeathDiscardsExpectedControlWithoutUndoingSpentReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	BloodEnvironment environment(gameState());
	auto model = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::DEFENDER);
	auto actor = enemy->acquireState();
	auto recipient = basilisk->acquireState();
	std::vector<petrifyingBloodAI::Pending> pending;
	petrifyingBloodAI::capture(model, *actor, *recipient, 1, false, true, pending);
	ASSERT_EQ(pending.size(), 1u);
	int64_t lethal = recipient->getAvailableHealth();
	recipient->damage(lethal);
	EXPECT_EQ(petrifyingBloodAI::resolve(model, *actor, pending, cache), 0);
	EXPECT_EQ(recipient->petrifyingBloodRound, battle()->getRound());
	EXPECT_FALSE(petrified(*actor));
}

TEST_F(NewHorizonsPetrifyingBloodAITest, FutureMultiattackCollectorWaitsForLaterSourceDeath)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	BloodEnvironment environment(gameState());
	auto model = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::DEFENDER);
	auto actor = model->getForUpdate(enemy->unitId());
	auto recipient = model->getForUpdate(basilisk->unitId());
	const int64_t oneHit = model->battleExpectedLuckDamage(BattleAttackInfo(actor.get(), recipient.get(), 0, false));
	ASSERT_GT(oneHit, 0);
	ASSERT_LT(oneHit + 1, recipient->getAvailableHealth());
	int64_t setupDamage = recipient->getAvailableHealth() - oneHit - 1;
	recipient->damage(setupDamage);
	std::vector<petrifyingBloodAI::Pending> pending;
	BattleExchangeVariant forecast;
	forecast.trackAttack(actor, recipient, false, true, cache, model,
		false, true, nullptr, nullptr, nullptr, nullptr, nullptr, &pending);
	ASSERT_TRUE(recipient->alive());
	ASSERT_EQ(pending.size(), 1u);
	forecast.trackAttack(actor, recipient, false, true, cache, model,
		false, false, nullptr, nullptr, nullptr, nullptr, nullptr, &pending);
	EXPECT_FALSE(recipient->alive());
	EXPECT_EQ(forecast.finishPetrifyingBlood(model, *actor, pending, true, cache), 0);
	EXPECT_EQ(pending.size(), 1u);
	EXPECT_FALSE(petrified(*actor));
}

TEST_F(NewHorizonsPetrifyingBloodAITest, SameAttackerSixNativeTwentyPercentAttemptsDoNotExceedOneOpportunity)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	std::vector<CStack *> sources{basilisk};
	for(const auto hex : {BattleHex(6, 1), BattleHex(9, 1), BattleHex(6, 3), BattleHex(9, 3), BattleHex(6, 7)})
		sources.push_back(addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:basilisk")), hex, 100));
	BloodEnvironment environment(gameState());
	auto model = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::DEFENDER);
	auto actor = enemy->acquireState();
	std::vector<std::shared_ptr<battle::CUnitState>> recipients;
	std::vector<petrifyingBloodAI::Pending> pending;
	for(const auto * source : sources)
	{
		recipients.push_back(source->acquireState());
		ASSERT_EQ(newHorizonsPetrifyingBlood::chanceBasisPoints(*model, *recipients.back(), *actor), 2000);
		petrifyingBloodAI::capture(model, *actor, *recipients.back(), 1, false, true, pending);
	}
	ASSERT_EQ(pending.size(), 6u);
	const float firstValue = petrifyingBloodAI::resolve(model, *recipients.front(), {pending.front()}, cache);
	ASSERT_GT(firstValue, 0);
	const float opportunity = firstValue / 0.2f;
	EXPECT_NEAR(petrifyingBloodAI::resolve(model, *recipients.front(), pending, cache),
		opportunity * 0.737856f, opportunity * 0.00001f);
	for(const auto & recipient : recipients)
		EXPECT_EQ(recipient->petrifyingBloodRound, battle()->getRound());
	EXPECT_FALSE(petrified(*actor));
}
TEST_F(NewHorizonsPetrifyingBloodAITest, DistinctAttackersRetainIndependentOpportunityBudgets)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * otherSource = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:basilisk")), BattleHex(6, 1), 100);
	const auto * otherEnemy = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::decode("core:pikeman")), BattleHex(10, 1), 10);
	BloodEnvironment environment(gameState());
	auto model = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::DEFENDER);
	auto first = enemy->acquireState();
	auto second = otherEnemy->acquireState();
	auto source = basilisk->acquireState();
	auto secondSource = otherSource->acquireState();
	std::vector<petrifyingBloodAI::Pending> pending;
	petrifyingBloodAI::capture(model, *first, *source, 1, false, true, pending);
	petrifyingBloodAI::capture(model, *second, *secondSource, 1, false, true, pending);
	ASSERT_EQ(pending.size(), 2u);
	const float separate = petrifyingBloodAI::resolve(model, *source, {pending[0]}, cache)
		+ petrifyingBloodAI::resolve(model, *source, {pending[1]}, cache);
	EXPECT_FLOAT_EQ(petrifyingBloodAI::resolve(model, *source, pending, cache), separate);
	EXPECT_FALSE(petrified(*first));
	EXPECT_FALSE(petrified(*second));
}

TEST_F(NewHorizonsPetrifyingBloodAITest, AlreadyPetrifiedTargetCreditsNoDuplicateButKeepsSpentReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::NOT_ACTIVE, BonusSource::SPELL_EFFECT, 1, BonusSourceID(SpellID(SpellID::STONE_GAZE))));
	BloodEnvironment environment(gameState());
	auto model = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::DEFENDER);
	auto actor = enemy->acquireState();
	auto recipient = basilisk->acquireState();
	std::vector<petrifyingBloodAI::Pending> pending;
	petrifyingBloodAI::capture(model, *actor, *recipient, 1, false, true, pending);
	ASSERT_EQ(pending.size(), 1u);
	EXPECT_EQ(petrifyingBloodAI::resolve(model, *recipient, pending, cache), 0);
	EXPECT_EQ(recipient->petrifyingBloodRound, battle()->getRound());
	EXPECT_TRUE(petrified(*actor));
}

namespace { class NewHorizonsBloodAerialAITest : public NewHorizonsBloodAerialFixture {}; }
TEST_F(NewHorizonsBloodAerialAITest, CompleteShotBindsBloodToFinalSurvivingCounter)
{
	ASSERT_NO_FATAL_FAILURE(prepareBoundary(false));
	BloodEnvironment environment(gameState());
	auto model = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto prediction = AttackPossibility::evaluate(BattleAttackInfo(model->battleGetUnitByID(shooter->unitId()),
		model->battleGetUnitByID(griffin->unitId()), 0, true), shooter->getPosition(), cache, model);
	ASSERT_TRUE(prediction.effectPreview);
	EXPECT_TRUE(prediction.effectPreview->battleGetUnitByID(griffin->unitId())->alive());
	EXPECT_FALSE(prediction.deferredBloodAttempts.empty());
	EXPECT_GT(prediction.petrifyingBloodControlValue, 0);
	EXPECT_FALSE(petrified(*griffin));
	EXPECT_EQ(basilisk->petrifyingBloodRound, -1);
}
TEST_F(NewHorizonsBloodAerialAITest, LaterShotDeathRemovesDeferredBloodControlCredit)
{
	ASSERT_NO_FATAL_FAILURE(prepareBoundary(true));
	BloodEnvironment environment(gameState());
	auto model = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto prediction = AttackPossibility::evaluate(BattleAttackInfo(model->battleGetUnitByID(shooter->unitId()),
		model->battleGetUnitByID(griffin->unitId()), 0, true), shooter->getPosition(), cache, model);
	ASSERT_TRUE(prediction.effectPreview);
	EXPECT_FALSE(prediction.effectPreview->battleGetUnitByID(griffin->unitId())->alive());
	EXPECT_FALSE(prediction.deferredBloodAttempts.empty());
	EXPECT_FLOAT_EQ(prediction.petrifyingBloodControlValue, 0);
	EXPECT_EQ(basilisk->petrifyingBloodRound, -1);
}
