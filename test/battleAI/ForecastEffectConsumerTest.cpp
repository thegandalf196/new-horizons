/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/ForecastNativeAdapter.h"
#include "../../AI/BattleAI/ForecastNativePreparation.h"
#include "../../AI/BattleAI/SearchBudget.h"
#include "../../lib/battle/BattleLayout.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/mapObjects/ObjectTemplate.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../server/battles/BattleProcessor.h"

namespace
{
class ForecastEffectConsumerTest : public TinyMapGameTest
{
protected:
	RecordingGameServer server;
	std::shared_ptr<CGameHandler> handler;
	std::unique_ptr<projectedHitTest::Environment> environment;
	std::shared_ptr<CBattleInfoCallback> visible;
	std::shared_ptr<HypotheticBattle> model;
	Services * gameServices() override { return LIBRARY; }
	void TearDown() override
	{
		model.reset(); visible.reset(); environment.reset(); handler.reset();
		server.gameState.reset(); TinyMapGameTest::TearDown();
	}
	std::shared_ptr<CArmedInstance> army(PlayerColor owner, const char * name)
	{
		auto value = std::make_shared<CArmedInstance>(gameState().get());
		value->tempOwner = owner; value->pos = int3(4, 4, 0);
		value->appearance = std::make_shared<ObjectTemplate>(); value->instanceName = name;
		value->setCreature(SlotID(0), CreatureID(CreatureID::decode("core:pikeman")), 100);
		map()->addNewObject(value); value->attachToBonusSystem(*gameState());
		return value;
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("Effect consumer parity")
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.town({15, 15, 0}, FactionID::CASTLE, PlayerColor(0)).townGarrison({})
			.town({25, 25, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({});
		ASSERT_NO_FATAL_FAILURE(startWithMap(std::move(builder), EMapDifficulty::NORMAL));
		server.gameState = gameState(); handler = std::make_shared<CGameHandler>(server, gameState());
		auto left = army(PlayerColor(0), "effectLeft"), right = army(PlayerColor(1), "effectRight");
		BattleLayout layout; layout.units[BattleSide::ATTACKER][0] = BattleHex(6, 5);
		layout.units[BattleSide::DEFENDER][0] = BattleHex(7, 5);
		layout.obstaclesAllowed = false; layout.tacticsAllowed = false;
		handler->battles->startBattle(left.get(), right.get(), int3(4, 4, 0), nullptr, nullptr, layout, nullptr);
		environment = std::make_unique<projectedHitTest::Environment>(gameState());
		visible = std::make_shared<CPlayerBattleCallback>(gameState()->getBattle(BattleID(0)), PlayerColor(0));
	}
	uint32_t first() const { return visible->battleGetAllUnits(false).front()->unitId(); }
	void rebuild()
	{
		model = std::make_shared<HypotheticBattle>(environment.get(), visible);
		ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*model));
		ASSERT_NE(model->forecastState(), nullptr);
	}
	void add(const Bonus & bonus)
	{
		SetStackEffect packet; packet.battleID = BattleID(0); packet.toAdd.emplace_back(first(), std::vector<Bonus>{bonus});
		handler->sendAndApply(packet);
	}
	std::shared_ptr<HypotheticBattle> child(std::shared_ptr<battleSearch::SearchBudget> budget = nullptr)
	{
		auto result = std::make_shared<HypotheticBattle>(environment.get(), model,
			std::shared_ptr<const battle::OwnedBattleFrameBatch>(), budget ? budget : model->searchBudget());
		forecast::NativeAdapter::forkExchange(*model, *result);
		return result;
	}
	uint32_t lazyTarget(const HypotheticBattle & branch)
	{
		for(const auto id : branch.forecastState()->ids())
			if(!branch.stackStates.contains(id)) return id;
		throw std::logic_error("Fixture did not preserve a lazy compatibility target");
	}
	Bonus timed(BonusType type, int value, int turns)
	{
		Bonus result(BonusDuration::N_TURNS, type, BonusSource::SPELL_EFFECT, value, BonusSourceID(SpellID(SpellID::HASTE)));
		result.turnsRemain = turns; return result;
	}
};
}

TEST_F(ForecastEffectConsumerTest, ActualApplicationKeepsTargetIdentityAndParentSiblingRng)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(rebuild());
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), model,
		std::shared_ptr<const battle::OwnedBattleFrameBatch>(), model->searchBudget());
	const auto id = first(); auto target = model->getForUpdate(id);
	const auto * identity = target.get();
	const auto source = projectedHitTest::serialized(*handler->randomizer);
	const auto ambient = projectedHitTest::serialized(CRandomGenerator::getDefault());
	const int before = target->getAttack(false);
	auto attack = timed(BonusType::PRIMARY_SKILL, 7, 3); attack.subtype = BonusSubtypeID(PrimarySkill::ATTACK);
	ASSERT_TRUE(forecast::NativeAdapter::effectApplication(*model, id, {}, {attack}));
	EXPECT_EQ(model->getForUpdate(id).get(), identity);
	EXPECT_EQ(target->getAttack(false), before + 7);
	ASSERT_NE(model->forecastState()->unit(id)->currentModifiers, nullptr);
	ASSERT_NE(model->forecastState()->prepared(id, false), nullptr);
	EXPECT_EQ(model->forecastState()->prepared(id, false)->unit.attack, target->getAttack(false));
	EXPECT_EQ(sibling->getForUpdate(id)->getAttack(false), before);
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), source);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), ambient);
	// Draft storage has already retired; target records/recipe remain owned.
	EXPECT_EQ(target->getAttack(false), before + 7);
}

TEST_F(ForecastEffectConsumerTest, RejectedPreparationLeavesTargetRecordsFlagsAndRevisionsUntouched)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(rebuild());
	const auto id = first(); auto target = model->getForUpdate(id);
	const auto state = model->forecastState()->revision(); const auto tree = target->getTreeVersion();
	const auto values = target->save(); const auto effects = model->forecastState()->unit(id)->effects;
	const auto attack = timed(BonusType::POISON, 7, 3);
	EXPECT_THROW(forecast::NativeAdapter::effectApplication(*model, id, {}, {attack}), std::invalid_argument);
	EXPECT_EQ(model->getForUpdate(id).get(), target.get());
	EXPECT_EQ(model->forecastState()->revision(), state);
	EXPECT_EQ(target->getTreeVersion(), tree);
	EXPECT_EQ(target->save(), values);
	EXPECT_EQ(model->forecastState()->unit(id)->effects, effects);
	EXPECT_TRUE(target->localSpellEffects().empty());
}

TEST_F(ForecastEffectConsumerTest, LateNativeRoundCaptureDoesNotAgeOrRefillSpentRetaliations)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	while(visible->battleGetRound() < 4)
	{
		BattleNextRound next; next.battleID = BattleID(0); handler->sendAndApply(next);
	}
	add(timed(BonusType::STACKS_SPEED, 1, 3));
	add(timed(BonusType::ADDITIONAL_RETALIATION, 1, 2));
	auto * native = const_cast<CStack *>(gameState()->getBattle(BattleID(0))->getStack(first(), false));
	ASSERT_NE(native, nullptr);
	ASSERT_GE(native->counterAttacks.available(), 2);
	native->counterAttacks.use(1);
	ASSERT_NO_FATAL_FAILURE(rebuild());
	const auto id = first();
	EXPECT_EQ(model->forecastState()->currentRound(), 4);
	ASSERT_NE(model->forecastState()->unit(id)->effects, nullptr);
	EXPECT_EQ(model->forecastState()->unit(id)->effects->round(), 4);
	EXPECT_EQ(model->getForUpdate(id)->counterAttacks.available(), 1);
	EXPECT_EQ(model->forecastState()->unit(id)->activation.retaliation.available, 1);
	EXPECT_EQ(model->forecastState()->unit(id)->effects->values()[0].value->turnsRemain, 3);
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), visible);
	ASSERT_EQ(reference->forecastState(), nullptr); // real legacy native lifecycle, no compact shadow
	for(int round = 5; round <= 7; ++round)
	{
		model->nextRound(); // ACTUAL adapter/HB round consumer, including final iteration
		reference->nextRound();
		const auto expected = reference->getForUpdate(id);
		EXPECT_EQ(model->forecastState()->currentRound(), round);
		EXPECT_EQ(model->getForUpdate(id)->counterAttacks.available(), expected->counterAttacks.available());
		EXPECT_EQ(model->getForUpdate(id)->getMovementRange(), expected->getMovementRange());
		EXPECT_EQ(model->getForUpdate(id)->localSpellEffects().size(), expected->localSpellEffects().size());
	}
	EXPECT_TRUE(model->forecastState()->unit(id)->effects->values().empty());
}

TEST_F(ForecastEffectConsumerTest, MaximumHealthRemovalUpdatesDetachedContextBeforeNextDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(rebuild());
	const auto id = first(); auto target = model->getForUpdate(id); const auto base = target->getMaxHealth();
	auto health = timed(BonusType::STACK_HEALTH, 4, 1);
	ASSERT_TRUE(forecast::NativeAdapter::effectApplication(*model, id, {}, {health}));
	ASSERT_EQ(target->getMaxHealth(), base + 4);
	EXPECT_EQ(model->forecastState()->unit(id)->health.valueContext().maximumHealth, base + 4);
	ASSERT_TRUE(forecast::NativeAdapter::effectApplication(*model, id, {health}, {}));
	EXPECT_EQ(target->getMaxHealth(), base);
	EXPECT_EQ(model->forecastState()->unit(id)->health.valueContext().maximumHealth, base);
	auto amount = int64_t(7);
	ASSERT_TRUE(forecast::NativeAdapter::damage(*model, *target, amount, battle::DamageProvenance::OTHER, nullptr));
	EXPECT_EQ(model->forecastState()->unit(id)->health.available(), target->health.available());
}

TEST_F(ForecastEffectConsumerTest, ChildPublicationKeepsImmutableParentAndSiblingNativeValues)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(rebuild());
	auto selected = child(); auto sibling = child();
	const auto id = first(); const auto before = model->getForUpdate(id)->getAttack(false);
	auto attack = timed(BonusType::PRIMARY_SKILL, 7, 3); attack.subtype = BonusSubtypeID(PrimarySkill::ATTACK);
	ASSERT_TRUE(forecast::NativeAdapter::effectApplication(*selected, id, {}, {attack}));
	EXPECT_EQ(selected->getForUpdate(id)->getAttack(false), before + 7);
	EXPECT_EQ(model->getForUpdate(id)->getAttack(false), before);
	EXPECT_EQ(sibling->getForUpdate(id)->getAttack(false), before);
}

TEST_F(ForecastEffectConsumerTest, LazyTypeAndBudgetRejectionPreservesLiveFriendlyObservation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(rebuild());
	auto budget = std::make_shared<battleSearch::SearchBudget>(battleSearch::WorkAmounts{8192, 128, 12, 12288, 4096});
	auto selected = child(budget); const auto id = lazyTarget(*selected);
	ASSERT_FALSE(selected->stackStates.contains(id));
	ASSERT_TRUE(selected->beginLifeDrainFriendlyObservation(id));
	const auto originalMap = selected->stackStates; const auto revision = selected->forecastState()->revision();
	auto poison = timed(BonusType::POISON, 5, 3);
	EXPECT_THROW(forecast::NativeAdapter::effectApplication(*selected, id, {}, {poison}), std::invalid_argument);
	EXPECT_EQ(selected->stackStates, originalMap);
	EXPECT_EQ(selected->forecastState()->revision(), revision);
	EXPECT_TRUE(selected->finishLifeDrainFriendlyObservation());
	ASSERT_TRUE(selected->beginLifeDrainFriendlyObservation(id));
	const auto room = budget->available()[static_cast<size_t>(battleSearch::WorkKind::Node)];
	ASSERT_TRUE(budget->consume(battleSearch::WorkKind::Node, room));
	auto attack = timed(BonusType::PRIMARY_SKILL, 5, 3); attack.subtype = BonusSubtypeID(PrimarySkill::ATTACK);
	EXPECT_THROW(forecast::NativeAdapter::effectApplication(*selected, id, {}, {attack}), battleSearch::BudgetExhausted);
	EXPECT_EQ(selected->stackStates, originalMap);
	EXPECT_EQ(selected->forecastState()->revision(), revision);
	EXPECT_TRUE(selected->finishLifeDrainFriendlyObservation());
}

TEST_F(ForecastEffectConsumerTest, LazyMapRollbackAfterActualFutureNumericPreparationRejection)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(rebuild());
	auto selected = child(); const auto id = lazyTarget(*selected);
	ASSERT_FALSE(selected->stackStates.contains(id));
	ASSERT_TRUE(selected->beginLifeDrainFriendlyObservation(id));
	const auto originalMap = selected->stackStates; const auto revision = selected->forecastState()->revision();
	bool reachedNumeric = false;
	EXPECT_THROW({
		auto draft = *selected->forecastState();
		auto attack = timed(BonusType::PRIMARY_SKILL, 7, 3); attack.subtype = BonusSubtypeID(PrimarySkill::ATTACK);
		const auto receipt = forecast::NativeAdapter::prepareEffectApplication(draft, *selected, id, {}, {attack});
		forecast::NativePreparation rollback(*selected);
		const auto target = rollback.unit(id);
		if(!selected->stackStates.contains(id)) throw std::logic_error("No lazy materialization witness");
		auto future = std::make_shared<HypotheticBattle>(environment.get(), selected,
			std::shared_ptr<const battle::OwnedBattleFrameBatch>(), selected->searchBudget());
		auto staged = forecast::NativeAdapter::prepareEffectDraft(*target, future.get());
		// Deliberately omit future->stackStates[id]=staged. The actual C4 seam
		// must reject this unregistered corresponding future peer AFTER staging.
		if(!forecast::NativeAdapter::stageEffects(draft, *selected, id, receipt, *staged))
		{
			reachedNumeric = !staged->localSpellEffects().empty();
			throw std::logic_error("Expected future-context numeric rejection");
		}
	}, std::logic_error);
	EXPECT_TRUE(reachedNumeric);
	EXPECT_EQ(selected->stackStates, originalMap);
	EXPECT_EQ(selected->forecastState()->revision(), revision);
	EXPECT_FALSE(selected->stackStates.contains(id));
	EXPECT_TRUE(selected->finishLifeDrainFriendlyObservation());
}
