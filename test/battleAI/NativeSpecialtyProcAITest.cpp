/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/NewHorizonsSpecialtyProcFixture.h"
#include "HeroQueueSelectorFixture.h"
#include "../../AI/BattleAI/AttackAbilityPrefix.h"
#include "../../AI/BattleAI/SpecialtyProcValue.h"
#include "../../AI/BattleAI/CreatureActivationValue.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
using namespace heroQueueTest;
class NativeSpecialtyProcAITest : public NewHorizonsSpecialtyProcFixture
{
protected:
	std::shared_ptr<QueueEnvironment> environment;
	std::shared_ptr<HypotheticBattle> parent, child, sibling;
	void branches(PlayerColor observer = PlayerColor(0))
	{
		environment = std::make_shared<QueueEnvironment>(gameState());
		parent = std::make_shared<HypotheticBattle>(environment.get(),
			std::make_shared<CPlayerBattleCallback>(battle(), observer));
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	}
	BattleAttackInfo hit(bool shooting = false) const
	{
		return {child->battleGetUnitByID(actor->unitId()), child->battleGetUnitByID(enemy->unitId()), 0, shooting};
	}
};
TEST_F(NativeSpecialtyProcAITest, BaseWyvernHasOneNativePoisonAttemptAndMonarchKeepsItsNativeChance)
{
	prepare("core:broghild", "core:wyvern");
	const auto * monarch = addStack(BattleSide::ATTACKER, creatureByName("core:wyvernMonarch"), BattleHex(7, 4), 5);
	ASSERT_NE(monarch, nullptr);
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto before = gameState()->saveToMemory();
	const auto rng = randomBytes(*gameHandler->randomizer);
	const auto query = attackAbilityPrefix::query(*child, *hit().attacker, *hit().defender, false);
	ASSERT_EQ(std::ranges::count_if(query.attempts, [](const auto & attempt) { return attempt.spell == SpellID::POISON; }), 1);
	const auto poison = std::ranges::find_if(query.attempts, [](const auto & attempt) { return attempt.spell == SpellID::POISON; });
	ASSERT_NE(poison, query.attempts.end());
	EXPECT_EQ(poison->chancePercent, 30);
	EXPECT_TRUE(poison->specialty);
	EXPECT_EQ(newHorizonsSpecialtyProcs::poisonPotencyMultiplier(*child, *hit().attacker), 1);
	const auto * projectedMonarch = child->battleGetUnitByID(monarch->unitId());
	const auto native = attackAbilityPrefix::query(*child, *projectedMonarch, *hit().defender, false);
	EXPECT_EQ(std::ranges::count_if(native.attempts, [](const auto & attempt) { return attempt.spell == SpellID::POISON; }), 1);
	EXPECT_FALSE(newHorizonsSpecialtyProcs::attackSpellChance(*child, *projectedMonarch, SpellID::POISON, false));
	EXPECT_EQ(newHorizonsSpecialtyProcs::poisonPotencyMultiplier(*child, *projectedMonarch), 2);
	EXPECT_EQ(randomBytes(*gameHandler->randomizer), rng);
	EXPECT_EQ(gameState()->saveToMemory(), before);
}
TEST_F(NativeSpecialtyProcAITest, ProjectedNextTurnRampsPoisonOncePerHealthRecordAndKeepsOtherBranchesIsolated)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:broghild", "core:wyvernMonarch"));
	passiveCast(SpellID(SpellID::POISON), *actor, *enemy);
	const auto healthSelector = Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::POISON))).And(Selector::type()(BonusType::STACK_HEALTH));
	const auto liveHealth = enemy->getBonus(healthSelector);
	ASSERT_NE(liveHealth, nullptr);
	ASSERT_EQ(liveHealth->val, -20);
	ASSERT_EQ(enemy->valOfBonuses(BonusType::POISON), 60);
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto before = gameState()->saveToMemory();
	const auto rng = randomBytes(*gameHandler->randomizer);
	const auto fixtureRng = wire(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto id = enemy->unitId();
	const auto expectHealth = [&](const std::shared_ptr<HypotheticBattle> & board, int value)
	{
		const auto records = board->battleGetUnitByID(id)->getAllBonuses(healthSelector);
		ASSERT_EQ(records->size(), 1);
		EXPECT_EQ(records->front()->val, value);
		EXPECT_EQ(records->front()->turnsRemain, liveHealth->turnsRemain);
		EXPECT_TRUE(newHorizonsSpecialtyProcs::isStrongWyvernPoison(records->front().get()));
	};
	const auto held = child->battleGetUnitByID(id)->getBonus(healthSelector);
	ASSERT_NE(held, nullptr);
	child->nextTurn(id, BattleUnitTurnReason::TURN_QUEUE);
	expectHealth(child, -40);
	EXPECT_EQ(held->val, -20);
	expectHealth(parent, -20);
	expectHealth(sibling, -20);
	child->nextTurn(id, BattleUnitTurnReason::TURN_QUEUE);
	expectHealth(child, -60);
	child->nextTurn(id, BattleUnitTurnReason::TURN_QUEUE);
	expectHealth(child, -60); // Native cap; repeated activation never duplicates the health bonus.
	EXPECT_EQ(child->battleGetUnitByID(id)->valOfBonuses(BonusType::POISON), 60);

	sibling->getForUpdate(id)->waiting = true;
	sibling->nextTurn(id, BattleUnitTurnReason::TURN_QUEUE);
	expectHealth(sibling, -20); // Resuming a waited activation does not ramp poison.
	sibling->getForUpdate(id)->waiting = false; // Isolate the missing-cap gate from the waiting gate.
	const auto poison = sibling->battleGetUnitByID(id)->getBonus(Selector::type()(BonusType::POISON));
	ASSERT_NE(poison, nullptr);
	sibling->getForUpdate(id)->removeUnitBonus(std::vector<Bonus>{*poison});
	ASSERT_FALSE(sibling->battleGetUnitByID(id)->hasBonusOfType(BonusType::POISON));
	sibling->nextTurn(id, BattleUnitTurnReason::TURN_QUEUE);
	expectHealth(sibling, -20); // A leftover health record without its Poison cap cannot ramp.

	auto stopped = std::make_shared<HypotheticBattle>(environment.get(), parent);
	stopped->getForUpdate(id)->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE,
		BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID())});
	ASSERT_TRUE(stopped->battleGetUnitByID(id)->isTimeStopped());
	stopped->nextTurn(id, BattleUnitTurnReason::TURN_QUEUE);
	expectHealth(stopped, -20);
	expectHealth(parent, -20);
	EXPECT_EQ(parent->battleGetUnitByID(id)->valOfBonuses(BonusType::POISON), 60);
	EXPECT_EQ(liveHealth->val, -20);
	EXPECT_EQ(enemy->getAllBonuses(healthSelector)->size(), 1);
	EXPECT_EQ(enemy->valOfBonuses(BonusType::POISON), 60);
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(randomBytes(*gameHandler->randomizer), rng);
	EXPECT_EQ(wire(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureRng);
}
TEST_F(NativeSpecialtyProcAITest, RepeatedYogHitsCreditOneFutureForfeitureAndCompletionCannotDoubleCredit)
{
	prepare("core:yog", "core:cyclop");
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto before = gameState()->saveToMemory();
	const auto rng = randomBytes(*gameHandler->randomizer);
	DamageCache cache;
	const auto context = hit();
	const auto pressure = creatureActivationValue::expectedTargetActivationValue(context.defender, cache, child, true);
	ASSERT_GT(pressure, 0);
	const auto info = attackAbilityPrefix::query(*child, *context.attacker, *context.defender, false);
	ASSERT_EQ(info.stunChancePercent, 25);
	ASSERT_EQ(info.initialNominalRerollAvailability, 0);
	specialtyProcAI::Opportunities opportunities;
	opportunities.observe(child, context);
	opportunities.observe(child, context);
	AttackPossibility preview(context.attacker->getPosition(), context.defender->getPosition(), context);
	opportunities.complete(preview, child, cache);
	EXPECT_NEAR(preview.defenderDamageReduce, pressure * (1.0 - 0.75 * 0.75), 0.001);
	const auto once = preview.defenderDamageReduce;
	opportunities.complete(preview, child, cache);
	EXPECT_EQ(preview.defenderDamageReduce, once);
	EXPECT_FALSE(newHorizonsStun::isStunned(*child->battleGetUnitByID(enemy->unitId())));
	EXPECT_FALSE(newHorizonsStun::isStunned(*sibling->battleGetUnitByID(enemy->unitId())));
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(randomBytes(*gameHandler->randomizer), rng);
}
TEST_F(NativeSpecialtyProcAITest, PendingOrDestroyedRecipientCannotEarnAnotherYogStatusValue)
{
	prepare("core:yog", "core:cyclop");
	status(*enemy, newHorizonsStun::marker());
	ASSERT_NO_FATAL_FAILURE(branches());
	DamageCache cache;
	const auto context = hit();
	specialtyProcAI::Opportunities pending;
	pending.observe(child, context);
	AttackPossibility preview(context.attacker->getPosition(), context.defender->getPosition(), context);
	pending.complete(preview, child, cache);
	EXPECT_EQ(preview.defenderDamageReduce, 0);
	child->getForUpdate(enemy->unitId())->removeUnitBonus(newHorizonsStun::removalPlan(*context.defender));
	specialtyProcAI::Opportunities dead;
	dead.observe(child, hit());
	const auto victim = child->getForUpdate(enemy->unitId());
	int64_t damage = victim->getAvailableHealth(); victim->damage(damage);
	dead.complete(preview, child, cache);
	EXPECT_EQ(preview.defenderDamageReduce, 0);
	EXPECT_TRUE(sibling->battleGetUnitByID(enemy->unitId())->alive());
	EXPECT_TRUE(newHorizonsStun::isStunned(*sibling->battleGetUnitByID(enemy->unitId())));
}
TEST_F(NativeSpecialtyProcAITest, TheodorusDispelValuesOnlyStillPresentPositiveTemporaryMagic)
{
	prepare("core:theodorus", "core:archMage");
	enemy->setPosition(BattleHex(9, 5));
	passiveCast(SpellID::BLESS, *enemy, *enemy);
	ASSERT_FALSE(newHorizonsSpecialtyProcs::dispelRemovalPlan(*enemy).empty());
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto before = gameState()->saveToMemory();
	const auto rng = randomBytes(*gameHandler->randomizer);
	const auto context = hit(true);
	const auto info = attackAbilityPrefix::query(*child, *context.attacker, *context.defender, true);
	const auto dispel = std::ranges::find_if(info.attempts, [](const auto & attempt) { return attempt.spell == SpellID::DISPEL_HELPFUL_SPELLS; });
	ASSERT_NE(dispel, info.attempts.end()); EXPECT_EQ(dispel->chancePercent, 40);
	DamageCache cache;
	specialtyProcAI::Opportunities opportunities;
	opportunities.observe(child, context);
	AttackPossibility preview(context.attacker->getPosition(), context.defender->getPosition(), context);
	opportunities.complete(preview, child, cache);
	EXPECT_GT(preview.defenderDamageReduce, 0);
	EXPECT_FALSE(newHorizonsSpecialtyProcs::dispelRemovalPlan(*child->battleGetUnitByID(enemy->unitId())).empty());
	specialtyProcAI::Opportunities expired;
	expired.observe(child, hit(true));
	child->getForUpdate(enemy->unitId())->removeUnitBonus(newHorizonsSpecialtyProcs::dispelRemovalPlan(*context.defender));
	AttackPossibility noEffect(context.attacker->getPosition(), context.defender->getPosition(), hit(true));
	expired.complete(noEffect, child, cache);
	EXPECT_EQ(noEffect.defenderDamageReduce, 0);
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(randomBytes(*gameHandler->randomizer), rng);
}
TEST_F(NativeSpecialtyProcAITest, FullAcceptedAttackPreviewAddsNoLiveStunOrNativeRandomDraw)
{
	prepare("core:yog", "core:cyclop");
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto before = gameState()->saveToMemory();
	const auto rng = randomBytes(*gameHandler->randomizer);
	const auto fixtureRng = wire(dynamic_cast<CRandomGenerator &>(*getRNG()));
	DamageCache cache;
	const auto context = hit();
	const auto preview = AttackPossibility::evaluate(context, context.attacker->getPosition(), cache, child);
	ASSERT_NE(preview.effectPreview, nullptr);
	EXPECT_FALSE(newHorizonsStun::isStunned(*child->battleGetUnitByID(enemy->unitId())));
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(randomBytes(*gameHandler->randomizer), rng);
	EXPECT_EQ(wire(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureRng);
}
TEST(NativeConditionalProcPrefix, OnlySuccessfulExactCurrentStatusRecordsCanSuppressAnotherAttempt)
{
	Bonus first, second, replacement;
	using attackAbilityPrefix::StatusKey;
	const StatusKey a{7, SpellID::DISPEL_HELPFUL_SPELLS, {&first}};
	const StatusKey b{7, SpellID::DISPEL_HELPFUL_SPELLS, {&second}};
	std::set<StatusKey> completed{a};
	EXPECT_TRUE(attackAbilityPrefix::alreadyApplied(completed, a));
	EXPECT_FALSE(attackAbilityPrefix::alreadyApplied(completed, {7, SpellID::DISPEL_HELPFUL_SPELLS, {&first, &second}}));
	completed.insert(b);
	EXPECT_TRUE(attackAbilityPrefix::alreadyApplied(completed, {7, SpellID::DISPEL_HELPFUL_SPELLS, {&first, &second}}));
	EXPECT_FALSE(attackAbilityPrefix::alreadyApplied(completed, {7, SpellID::DISPEL_HELPFUL_SPELLS, {&replacement}}));
	EXPECT_FALSE(attackAbilityPrefix::alreadyApplied(completed, {8, SpellID::DISPEL_HELPFUL_SPELLS, {&first}}));
	const StatusKey stun{7, SpellID::NONE, {}};
	EXPECT_FALSE(attackAbilityPrefix::alreadyApplied(completed, stun));
	completed.insert(stun);
	EXPECT_TRUE(attackAbilityPrefix::alreadyApplied(completed, stun));
}
