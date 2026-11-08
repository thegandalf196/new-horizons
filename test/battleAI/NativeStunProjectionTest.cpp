/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../../AI/BattleAI/ActivationForfeit.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../AI/BattleAI/SelectedProbeDamage.h"
#include "../../AI/BattleAI/BattleExchangeAftermath.h"
#include "../../AI/BattleAI/BattleSearchPolicy.h"
#include "../../AI/BattleAI/CreatureCandidateScreening.h"
#include <limits>
#include <cmath>
#include "../../AI/BattleAI/HeroSpellVariants.h"
#include "../../AI/BattleAI/HeroPurifyProjection.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../lib/spells/NewHorizonsPurify.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
using namespace heroQueueTest;
class NativeStunProjectionTest : public HeroQueueSelectorTest
{
protected:
	NativeStunProjectionTest() { useRevisedOrders = true; }
	void prepare()
	{
		ASSERT_NO_FATAL_FAILURE(world());
		ASSERT_NO_FATAL_FAILURE(arena());
	}
	void branches()
	{
		environment = std::make_shared<QueueEnvironment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	}
};
TEST_F(NativeStunProjectionTest, ActualActivationForfeitsOnceAndLeavesParentSiblingAndNormalRetaliationUntouched)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	effect(actor, newHorizonsStun::marker());
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	const auto unit = child->getForUpdate(actor->unitId());
	ASSERT_TRUE(unit->ableToRetaliate());
	child->nextTurn(unit->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_TRUE(child->currentForfeitCompleted(unit->unitId()));
	EXPECT_FALSE(newHorizonsStun::isStunned(*unit));
	EXPECT_TRUE(unit->ableToRetaliate());
	DamageCache cache;
	EXPECT_TRUE(PotentialTargets(unit.get(), cache, child).possibleAttacks.empty());
	const auto after = state(*child);
	child->completeForfeitedActivation(unit->unitId());
	child->finishProjectedActivation(unit->unitId());
	EXPECT_EQ(state(*child), after);
	EXPECT_TRUE(newHorizonsStun::isStunned(*parent->getForUpdate(actor->unitId())));
	EXPECT_TRUE(newHorizonsStun::isStunned(*sibling->getForUpdate(actor->unitId())));
	EXPECT_EQ(observation(), outside);
}
TEST_F(NativeStunProjectionTest, WaitContinuationsAndTimeStopCannotConsumePendingStun)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	effect(actor, newHorizonsStun::marker());
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	const auto unit = child->getForUpdate(actor->unitId());
	child->makeWait(unit.get());
	EXPECT_TRUE(newHorizonsStun::isStunned(*unit));
	child->nextTurn(unit->unitId(), BattleUnitTurnReason::PURSUIT_CONTINUATION);
	EXPECT_TRUE(newHorizonsStun::isStunned(*unit));
	EXPECT_FALSE(child->wasCurrentActivationForfeited(unit->unitId()));
	child->addUnitBonus(unit->unitId(), {Bonus(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP,
		BonusSource::OTHER, 1, BonusSourceID())});
	child->nextTurn(unit->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_TRUE(newHorizonsStun::isStunned(*unit));
	EXPECT_FALSE(child->wasCurrentActivationForfeited(unit->unitId()));
	EXPECT_EQ(observation(), outside);
}
TEST_F(NativeStunProjectionTest, MoraleAndReducedExtrasSpendOnlyTheirActualForfeiture)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	effect(actor, newHorizonsStun::marker());
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	for(const auto reason : {BattleUnitTurnReason::MORALE, BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION})
	{
		auto branch = std::make_shared<HypotheticBattle>(environment.get(), parent);
		branch->nextTurn(actor->unitId(), reason);
		EXPECT_TRUE(branch->currentForfeitCompleted(actor->unitId()));
		EXPECT_FALSE(newHorizonsStun::isStunned(*branch->getForUpdate(actor->unitId())));
		branch->nextRound();
		branch->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
		EXPECT_FALSE(branch->wasCurrentActivationForfeited(actor->unitId()));
		DamageCache cache;
		EXPECT_FALSE(PotentialTargets(branch->getForUpdate(actor->unitId()).get(), cache, branch).possibleAttacks.empty());
	}
	EXPECT_EQ(observation(), outside);
}
TEST_F(NativeStunProjectionTest, ConsumedSecondWindClearsActiveFlagAndPaysOnlyIsolatedResources)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	actor->movedThisRound = true;
	effect(actor, newHorizonsStun::marker());
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	const auto pool = child->getCommandResources(BattleSide::ATTACKER);
	const auto allowance = child->prepareHeroOrderAllowance(BattleSide::ATTACKER);
	ASSERT_TRUE(allowance);
	const auto effects = child->beginProjectedHeroOrder(BattleSide::ATTACKER, HeroCommand::SECOND_WIND,
		{actor->unitId()}, *allowance);
	ASSERT_TRUE(effects);
	ASSERT_TRUE(child->projectAcceptedHeroOrder(BattleSide::ATTACKER, *effects, *allowance));
	const auto continuation = child->projectOrderContinuation(BattleSide::ATTACKER, *allowance, *effects);
	ASSERT_EQ(continuation.kind, HypotheticBattle::ProjectedOrderContinuation::Kind::SECOND_WIND_ACTOR);
	EXPECT_TRUE(child->currentForfeitCompleted(actor->unitId()));
	EXPECT_FALSE(newHorizonsStun::isStunned(*child->getForUpdate(actor->unitId())));
	const auto order = child->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND);
	ASSERT_TRUE(order); EXPECT_FALSE(order->secondWindActive);
	EXPECT_EQ(child->getCommandResources(BattleSide::ATTACKER).remaining, pool.remaining - 1);
	EXPECT_EQ(parent->getCommandResources(BattleSide::ATTACKER), pool);
	child->nextRound(); child->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(child->wasCurrentActivationForfeited(actor->unitId()));
	EXPECT_EQ(observation(), outside);
}
TEST_F(NativeStunProjectionTest, NoActionExchangeRetainsRemainingActorsAndTheFollowingRound)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	effect(actor, newHorizonsStun::marker());
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	child->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	const auto unit = child->getForUpdate(actor->unitId());
	ASSERT_TRUE(child->currentForfeitCompleted(unit->unitId()));
	DamageCache firstCache, secondCache;
	BattleExchangeEvaluator first(child, environment, 1, 1), second(child, environment, 1, 2);
	first.updateReachabilityMap(child); second.updateReachabilityMap(child);
	const auto remaining = first.evaluateNoActionContinuation(unit.get(), firstCache, child);
	const auto following = second.evaluateNoActionContinuation(unit.get(), secondCache, child);
	EXPECT_NE(remaining, 0); // Other friendly and enemy stacks still exchange attacks.
	EXPECT_GT(following, remaining); // The strong forfeited Angel acts in the ordinary next round.
	EXPECT_EQ(observation(), outside);
}
TEST_F(NativeStunProjectionTest, InvalidMetadataAnchorCannotBypassTheCompletedForfeitureProof)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(branches());
	DamageCache cache;
	BattleExchangeEvaluator evaluator(child, environment, 1, 1); evaluator.updateReachabilityMap(child);
	const auto source = child->getForUpdate(actor->unitId());
	const auto target = child->getForUpdate(enemy->unitId());
	AttackPossibility forged(source->getPosition(), target->getPosition(), BattleAttackInfo(source.get(), target.get(), 0, false));
	forged.noActionAnchor = true;
	PotentialTargets targets(source.get(), cache, child);
	EXPECT_THROW(evaluator.evaluateExchange(forged, 0, targets, cache, child), std::invalid_argument);
	EXPECT_THROW(evaluator.evaluateNoActionContinuation(source.get(), cache, child), std::invalid_argument);
}
TEST_F(NativeStunProjectionTest, FullHealthCureOffersPhysicalStunWithoutClaimingFrozenRecovery)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	effect(actor, newHorizonsStun::marker());
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	const auto * cure = SpellID(SpellID::CURE).toSpell();
	const auto variants = heroSpellVariants::prepare(*child, attackerSideHero, cure, BattleSide::ATTACKER);
	EXPECT_NE(std::ranges::find(variants.cureAfflictionChoices, std::pair{SpellID(SpellID::NONE), std::string("stun")}), variants.cureAfflictionChoices.end());
	const auto * before = parent->battleGetUnitByID(actor->unitId()); // Immutable bonus provider.
	ASSERT_TRUE(newHorizonsStun::isStunned(*before));
	const auto after = child->getForUpdate(actor->unitId());
	after->removeUnitBonus(newHorizonsStun::removalPlan(*after));
	ASSERT_FALSE(newHorizonsStun::isStunned(*after));
	EXPECT_TRUE(heroSpellVariants::recoveredCreatureActivation("stun", *before, *after));
	after->addUnitBonus({newHorizonsFrozen::makeFrozenMarker(BonusSourceID(creatureByName("core:iceElemental")), child->getRound())});
	EXPECT_FALSE(heroSpellVariants::recoveredCreatureActivation("stun", *before, *after));
	after->addUnitBonus({newHorizonsStun::marker()});
	const auto * both = child->battleGetUnitByID(actor->unitId());
	ASSERT_TRUE(newHorizonsFrozen::isFrozen(*both));
	ASSERT_TRUE(newHorizonsStun::isStunned(*both));
	auto withoutFrozen = std::make_shared<HypotheticBattle>(environment.get(), child);
	const auto thawed = withoutFrozen->getForUpdate(actor->unitId());
	thawed->removeUnitBonus(newHorizonsFrozen::removalPlan(*thawed));
	ASSERT_FALSE(newHorizonsFrozen::isFrozen(*thawed));
	ASSERT_TRUE(newHorizonsStun::isStunned(*thawed));
	EXPECT_FALSE(heroSpellVariants::recoveredCreatureActivation("frozen", *both, *thawed));
	EXPECT_EQ(observation(), outside);
}
TEST_F(NativeStunProjectionTest, EndOfForfeitureRunsPlagueAndUntilActivationEndsExactlyOnce)
{
	ASSERT_NO_FATAL_FAILURE(world());
	const SpellID plague(SpellID::decode("new-horizons:plague"));
	ASSERT_TRUE(plague.hasValue());
	ASSERT_NO_FATAL_FAILURE(book(defenderSideHero, plague, "new-horizons:shadowMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(activate(enemy));
	ASSERT_TRUE(cast(defenderSideHero, plague, actor));
	ASSERT_NO_FATAL_FAILURE(activate(active));
	effect(actor, newHorizonsStun::marker());
	effect(actor, Bonus(BonusDuration::STACK_ACTIVATION, BonusType::PRIMARY_SKILL,
		BonusSource::OTHER, 7, BonusSourceID(BonusCustomSource(77)), BonusSubtypeID(PrimarySkill::ATTACK)));
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	child->nextTurnPreservingForfeit(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	const auto before = child->getForUpdate(actor->unitId())->getAvailableHealth();
	child->completeForfeitedActivation(actor->unitId());
	EXPECT_LT(child->getForUpdate(actor->unitId())->getAvailableHealth(), before);
	EXPECT_EQ(child->getProjectedPlagueHits().size(), 1u);
	EXPECT_FALSE(child->getForUpdate(actor->unitId())->hasBonus(Selector::source(BonusSource::OTHER, BonusSourceID(BonusCustomSource(77)))));
	const auto after = state(*child);
	child->completeForfeitedActivation(actor->unitId());
	child->finishProjectedActivation(actor->unitId());
	EXPECT_EQ(child->getProjectedPlagueHits().size(), 1u);
	EXPECT_EQ(state(*child), after);
	EXPECT_EQ(observation(), outside);
}
TEST_F(NativeStunProjectionTest, PurifierStunOnlyAreaGetsRecoveryValueAndAutomaticProjectionWithoutExtraChoices)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, newHorizonsPurify::spellID(), std::string(newHorizonsPurify::LIGHT_MAGIC_SKILL)));
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, std::string(newHorizonsPurify::LIGHT_MAGIC_SKILL),
		"new-horizons:lightMagic.healer", MasteryLevel::BASIC));
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, std::string(newHorizonsPurify::LIGHT_MAGIC_SKILL),
		std::string(newHorizonsPurify::PURIFIER_PERK), MasteryLevel::ADVANCED));
	ASSERT_TRUE(newHorizonsPurify::hasPurifierPerk(attackerSideHero));
	ASSERT_NO_FATAL_FAILURE(arena());
	effect(actor, newHorizonsStun::marker());
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	const auto * spell = newHorizonsPurify::spellID().toSpell();
	spells::BattleCast cast(child.get(), attackerSideHero, spells::Mode::HERO, spell);
	spells::Target target; target.emplace_back(actor->getPosition());
	const auto mechanics = spell->battleMechanics(&cast);
	const auto selection = SpellTargetEvaluator::purifySelection(mechanics.get(), target);
	EXPECT_GT(selection.value, 0);
	EXPECT_TRUE(selection.spellEffectGroups.empty());
	EXPECT_TRUE(selection.physicalPoisonStackIds.empty());
	BattleAction action; action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER; action.spell = newHorizonsPurify::spellID();
	action.aimToHex(actor->getPosition());
	const auto recipients = heroSpellProjection::projectAutomaticPurifyEffects(*child, action, attackerSideHero);
	ASSERT_TRUE(recipients);
	EXPECT_NE(std::ranges::find(*recipients, actor->unitId()), recipients->end());
	EXPECT_FALSE(newHorizonsStun::isStunned(*child->getForUpdate(actor->unitId())));
	EXPECT_TRUE(newHorizonsStun::isStunned(*parent->getForUpdate(actor->unitId())));
	EXPECT_TRUE(newHorizonsStun::isStunned(*sibling->getForUpdate(actor->unitId())));
	EXPECT_EQ(observation(), outside);
}
TEST_F(NativeStunProjectionTest, NoActionContinuationHandlesDeadActorAndNoHostileWithoutInventingAnAttack)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(4, 5), 1000), nullptr);
	effect(actor, newHorizonsStun::marker());
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	child->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	auto dead = std::make_shared<HypotheticBattle>(environment.get(), child);
	const auto corpse = dead->getForUpdate(actor->unitId());
	int64_t lethal = corpse->getAvailableHealth(); corpse->damage(lethal);
	DamageCache deadCache;
	BattleExchangeEvaluator remaining(dead, environment, 1, 1); remaining.updateReachabilityMap(dead);
	EXPECT_LT(remaining.evaluateNoActionContinuation(corpse.get(), deadCache, dead), 0);
	auto noHostiles = std::make_shared<HypotheticBattle>(environment.get(), child);
	for(const auto * unit : noHostiles->battleGetAllUnits(false))
		if(unit->unitSide() == BattleSide::DEFENDER)
		{
			const auto target = noHostiles->getForUpdate(unit->unitId());
			lethal = target->getAvailableHealth(); target->damage(lethal);
		}
	DamageCache noHostileCache;
	BattleExchangeEvaluator empty(noHostiles, environment, 1, 1); empty.updateReachabilityMap(noHostiles);
	EXPECT_EQ(empty.evaluateNoActionContinuation(noHostiles->getForUpdate(actor->unitId()).get(), noHostileCache, noHostiles), 0);
	EXPECT_EQ(observation(), outside);
}

TEST_F(NativeStunProjectionTest, NoActionFullMembershipDoesNotGrantRemoteMeleeDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	active->setPosition(BattleHex(1, 1)); actor->setPosition(BattleHex(1, 3));
	waiter->setPosition(BattleHex(2, 5)); enemy->setPosition(BattleHex(15, 5));
	for(const auto * unit : {active, actor, waiter, enemy})
	{
		effect(unit, Bonus(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
			BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(), BonusValueType::INDEPENDENT_MIN));
		effect(unit, Bonus(BonusDuration::ONE_BATTLE, BonusType::STACKS_MOVEMENT_RANGE,
			BonusSource::OTHER, 0, BonusSourceID(), BonusSubtypeID(), BonusValueType::INDEPENDENT_MIN));
		ASSERT_EQ(unit->getMovementRange(), 1u);
		ASSERT_EQ(unit->getMovementRange(1), 1u);
		ASSERT_FALSE(unit->isShooter());
	}
	const auto * archer = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(2, 3), 50);
	ASSERT_NE(archer, nullptr);
	effect(active, newHorizonsStun::marker());
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	const auto absentArcher = child->getForUpdate(archer->unitId());
	int64_t lethal = absentArcher->getAvailableHealth(); absentArcher->damage(lethal);
	child->nextTurn(active->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	DamageCache cache;
	BattleExchangeEvaluator evaluator(child, environment, 1, 2); evaluator.updateReachabilityMap(child);
	EXPECT_EQ(evaluator.evaluateNoActionContinuation(child->getForUpdate(active->unitId()).get(), cache, child), 0);
	auto ranged = std::make_shared<HypotheticBattle>(environment.get(), parent);
	ranged->nextTurn(active->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	ASSERT_TRUE(ranged->battleCanShootAction(ranged->battleGetUnitByID(archer->unitId()), enemy->getPosition()));
	DamageCache rangedCache;
	BattleExchangeEvaluator shooter(ranged, environment, 1, 2); shooter.updateReachabilityMap(ranged);
	EXPECT_GT(shooter.evaluateNoActionContinuation(ranged->getForUpdate(active->unitId()).get(), rangedCache, ranged), 0);
	EXPECT_EQ(observation(), outside);
}

TEST_F(NativeStunProjectionTest, NoActionEnemyCanChooseReachableForfeitedActorOverAnotherReachableAlly)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * low = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(9, 5), 1);
	ASSERT_NE(low, nullptr);
	effect(actor, newHorizonsStun::marker()); effect(low, newHorizonsStun::marker());
	effect(enemy, Bonus(BonusDuration::ONE_BATTLE, BonusType::BLOCKS_RETALIATION, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	for(const auto * source : {active, waiter})
	{
		const auto unit = child->getForUpdate(source->unitId());
		int64_t lethal = unit->getAvailableHealth(); unit->damage(lethal);
	}
	child->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	auto oracle = std::make_shared<HypotheticBattle>(environment.get(), child);
	oracle->nextTurn(enemy->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	const auto attacker = oracle->getForUpdate(enemy->unitId());
	const auto strong = oracle->getForUpdate(actor->unitId());
	const auto weak = oracle->getForUpdate(low->unitId());
	DamageCache expectedCache;
	BattleExchangeVariant expected;
	const auto strongValue = expected.trackAttack(attacker, strong, false, false, expectedCache, oracle, true);
	const auto weakValue = expected.trackAttack(attacker, weak, false, false, expectedCache, oracle, true);
	ASSERT_GT(strongValue, weakValue); // The public probe kernel strictly prefers the forfeited actor.
	expected.trackAttack(attacker, strong, false, false, expectedCache, oracle);
	std::vector<battle::Units> queue;
	child->battleGetTurnOrder(queue, std::numeric_limits<int>::max(), 2);
	const auto normalized = battleExchangeAftermath::finalizeExchangeScore(expected.getScore(), 1,
		std::max(1, static_cast<int>(queue.size())), 0);
	const auto expectedValue = normalized.enemyDamageReduce - normalized.ourDamageReduce;
	DamageCache cache;
	BattleExchangeEvaluator evaluator(child, environment, 1, 1); evaluator.updateReachabilityMap(child);
	EXPECT_NEAR(evaluator.evaluateNoActionContinuation(child->getForUpdate(actor->unitId()).get(), cache, child), expectedValue, 0.001);
	EXPECT_LT(expectedValue, 0);
	EXPECT_EQ(observation(), outside);
}

TEST_F(NativeStunProjectionTest, QuickNoActionCapStopsBeforeTheThirdRemainingActivation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	effect(actor, newHorizonsStun::marker());
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	child->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	const auto unit = child->getForUpdate(actor->unitId());
	ASSERT_TRUE(child->currentForfeitCompleted(unit->unitId()));
	ASSERT_EQ(child->battleAliveUnits().size(), 4u); // Three real remaining actors, not a mirrored budget loop.
	const auto before = state(*child);
	BattleAttackInfo metadata(unit.get(), unit.get(), 0, false);
	AttackPossibility anchor(unit->getPosition(), unit->getPosition(), metadata);
	anchor.noActionAnchor = true;
	const auto score = [&](battleSearch::Profile profile, BattleMovementTiming::ExchangeTiming & timing)
	{
		const battleSearch::Scope scope(profile);
		DamageCache cache;
		PotentialTargets targets(unit.get(), cache, child);
		EXPECT_TRUE(targets.possibleAttacks.empty()); // Completed current slot cannot acquire a chosen AP.
		BattleExchangeEvaluator evaluator(child, environment, 1, 1);
		evaluator.updateReachabilityMap(child);
		return evaluator.evaluateExchange(anchor, 0, targets, cache, child, &timing);
	};
	{
		const battleSearch::Scope quick(battleSearch::Profile::BeamOneTwoActors);
		ASSERT_EQ(creatureCandidateScreening::admittedFutureActorLimit(unit.get(), *child, false), 2u);
	}
	BattleMovementTiming::ExchangeTiming full, quick;
	EXPECT_TRUE(std::isfinite(score(battleSearch::Profile::Full, full)));
	EXPECT_TRUE(std::isfinite(score(battleSearch::Profile::BeamOneTwoActors, quick)));
	EXPECT_GE(full.activationStart.calls, 3u);
	EXPECT_EQ(quick.activationStart.calls, 2u);
	EXPECT_GE(quick.activationEnd.calls, 2u); // Prior actors reach their completion path before the cap.
	EXPECT_EQ(quick.roundAdvance.calls, 0u); // Partial scheduled round must not advance.
	EXPECT_EQ(state(*child), before);
	EXPECT_EQ(observation(), outside);
}

TEST_F(NativeStunProjectionTest, QuickNoActionNextRoundAnchorConsumesTheSecondFutureSlot)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	// Two surviving participants isolate the cross-round case: one remaining
	// current-round actor, then the forfeited Angel returns as an ordinary actor.
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : {active, waiter, enemy})
		remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	active = actor;
	waiter = nullptr;
	enemy = addStack(BattleSide::DEFENDER, creatureByName("core:stoneGolem"), BattleHex(8, 5), 1000);
	ASSERT_NE(enemy, nullptr);
	activate(actor);
	effect(actor, newHorizonsStun::marker());
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	child->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	const auto unit = child->getForUpdate(actor->unitId());
	ASSERT_TRUE(child->currentForfeitCompleted(unit->unitId()));
	ASSERT_EQ(child->battleAliveUnits().size(), 2u);
	const auto before = state(*child);
	BattleAttackInfo metadata(unit.get(), unit.get(), 0, false);
	AttackPossibility anchor(unit->getPosition(), unit->getPosition(), metadata);
	anchor.noActionAnchor = true;
	const auto score = [&](battleSearch::Profile profile, BattleMovementTiming::ExchangeTiming & timing)
	{
		const battleSearch::Scope scope(profile);
		DamageCache cache;
		PotentialTargets targets(unit.get(), cache, child);
		EXPECT_TRUE(targets.possibleAttacks.empty());
		BattleExchangeEvaluator evaluator(child, environment, 1, 2);
		evaluator.updateReachabilityMap(child);
		return evaluator.evaluateExchange(anchor, 0, targets, cache, child, &timing);
	};
	{
		const battleSearch::Scope quick(battleSearch::Profile::BeamOneTwoActors);
		ASSERT_EQ(creatureCandidateScreening::admittedFutureActorLimit(unit.get(), *child, false), 2u);
	}
	BattleMovementTiming::ExchangeTiming full, quick;
	EXPECT_TRUE(std::isfinite(score(battleSearch::Profile::Full, full)));
	EXPECT_TRUE(std::isfinite(score(battleSearch::Profile::BeamOneTwoActors, quick)));
	EXPECT_GE(full.activationStart.calls, 3u);
	EXPECT_EQ(quick.activationStart.calls, 2u); // No initial-AP exemption when the Angel reappears.
	EXPECT_EQ(quick.roundAdvance.calls, 1u); // First round complete; second stops before the third body.
	EXPECT_GE(quick.activationEnd.calls, 2u);
	EXPECT_GE(quick.appliedAttacks.calls, 2u); // Both accepted bodies include their whole native hit/reaction path.
	EXPECT_EQ(state(*child), before);
	EXPECT_EQ(observation(), outside);
}
