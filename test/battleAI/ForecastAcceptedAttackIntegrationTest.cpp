/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapterFixture.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../AI/BattleAI/ProjectedBestAction.h"
#include "../../lib/battle/BattleEffectExchange.h"
namespace
{
AttackPossibility acceptedPreview(const std::shared_ptr<HypotheticBattle> & source,
	uint32_t attacker, uint32_t defender, bool ranged)
{
	DamageCache cache;
	cache.buildDamageCache(source, source->battleGetUnitByID(attacker)->unitSide());
	const auto world = std::make_shared<const VisibleCandidateWorld>(source);
	return VisibleCandidateWorld::evaluate(world, source->battleGetUnitByID(attacker),
		source->battleGetUnitByID(defender), 0, ranged,
		source->battleGetUnitByID(attacker)->getPosition(), cache);
}
void expectSame(const HypotheticBattle & actual, const HypotheticBattle & expected)
{
	for(const auto * unit : actual.battleGetAllUnits(false))
	{
		const auto * other = expected.battleGetUnitByID(unit->unitId());
		ASSERT_NE(other, nullptr);
		EXPECT_EQ(unit->acquireState()->save(), other->acquireState()->save());
		EXPECT_EQ(newHorizonsFocusMagic::charges(*unit), newHorizonsFocusMagic::charges(*other));
		const auto * values = actual.forecastState()->unit(unit->unitId());
		EXPECT_EQ(values->health.available(), unit->getAvailableHealth());
		EXPECT_EQ(values->health.getCount(), unit->getCount());
		EXPECT_EQ(values->activation.shots.available, unit->acquireState()->shots.available());
		EXPECT_EQ(values->activation.retaliation.available, unit->acquireState()->counterAttacks.available());
	}
}
}
TEST_F(ForecastNativeAdapterTest, AcceptedWholeAttackBothRoutesPreserveNativeStatePointersParentsAndRng)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	const auto ids = parent->forecastState()->ids();
	ASSERT_EQ(ids.size(), 2u);
	const auto rng = projectedHitTest::serialized(*handler->randomizer);
	const auto ambient = projectedHitTest::serialized(CRandomGenerator::getDefault());
	const auto before = parent->getForUpdate(ids[0])->save();
	const auto action = acceptedPreview(parent, ids[0], ids[1], false);
	ASSERT_TRUE(action.forecastSource);
	for(const auto route : {forecast::AcceptedAttackRoute::BestAction, forecast::AcceptedAttackRoute::HeroQueue})
	{
		auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
		auto reference = std::make_shared<HypotheticBattle>(environment.get(), parent);
		forecast::NativeAdapter::forkExchange(*parent, *child);
		forecast::NativeAdapter::forkExchange(*parent, *sibling);
		auto actor = child->getForUpdate(ids[0]);
		auto victim = child->getForUpdate(ids[1]);
		forecast::NativeAdapter::replayAcceptedAttackNative(*reference, actor.get(), action, route);
		if(route == forecast::AcceptedAttackRoute::BestAction)
			applyProjectedBestAction(*child, actor.get(), action); // Actual hot-loop entry.
		else ASSERT_TRUE(forecast::NativeAdapter::applyAcceptedAttack(*child, actor.get(), action, route));
		expectSame(*child, *reference);
		EXPECT_EQ(child->battleGetUnitByID(ids[0]), actor.get());
		EXPECT_EQ(child->battleGetUnitByID(ids[1]), victim.get());
		EXPECT_EQ(parent->getForUpdate(ids[0])->save(), before);
		EXPECT_EQ(sibling->getForUpdate(ids[0])->save(), before);
		EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), rng);
		EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), ambient);
	}
}
TEST_F(ForecastNativeAdapterTest, AcceptedFocusSnapshotImportsThreeToTwoThenFinalEmptyWithoutDoublePayment)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto units = parent->battleGetAllUnits(false);
	const auto id = units[0]->unitId(), defender = units[1]->unitId();
	auto original = parent->getForUpdate(id);
	original->setPosition(BattleHex(2, 5));
	int64_t loss = original->getAvailableHealth() - 2 * original->getMaxHealth();
	original->damage(loss, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	original->addUnitBonus(newHorizonsFocusMagic::enchantment(*original,
		SpellID(SpellID::decode(newHorizonsFocusMagic::SPELL))));
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	for(const auto route : {forecast::AcceptedAttackRoute::BestAction, forecast::AcceptedAttackRoute::HeroQueue})
	{
		auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		auto reference = std::make_shared<HypotheticBattle>(environment.get(), parent);
		forecast::NativeAdapter::forkExchange(*parent, *child);
		auto actor = child->getForUpdate(id);
		for(int remaining = 2; remaining >= 0; --remaining)
		{
			const auto action = acceptedPreview(child, id, defender, true);
			ASSERT_EQ(newHorizonsFocusMagic::charges(*action.attackerState), remaining);
			forecast::NativeAdapter::replayAcceptedAttackNative(*reference, actor.get(), action, route);
			const auto once = reference->getForUpdate(id)->save();
			forecast::NativeAdapter::replayAcceptedAttackNative(*reference, actor.get(), action, route);
			EXPECT_EQ(reference->getForUpdate(id)->save(), once); // Receipt import is idempotent.
			ASSERT_TRUE(forecast::NativeAdapter::applyAcceptedAttack(*child, actor.get(), action, route));
			EXPECT_EQ(newHorizonsFocusMagic::charges(*actor), remaining);
			EXPECT_EQ(child->forecastState()->unit(id)->focus->charges, remaining);
			expectSame(*child, *reference);
		}
		EXPECT_FALSE(actor->isShooter());
		EXPECT_TRUE(child->forecastState()->unit(id)->focus->records.empty());
		int64_t damage = actor->getMaxHealth(), expected = damage;
		reference->getForUpdate(id)->damage(expected, false, battle::DamageProvenance::PHYSICAL_CREATURE);
		ASSERT_TRUE(forecast::NativeAdapter::damage(*child, *actor, damage, battle::DamageProvenance::PHYSICAL_CREATURE, nullptr));
		EXPECT_FALSE(actor->isShooter()); // Current C4 recipe cannot resurrect Focus.
		expectSame(*child, *reference);
	}
	EXPECT_EQ(newHorizonsFocusMagic::charges(*original), 3);
}
TEST_F(ForecastNativeAdapterTest, AcceptedNativeArcherWithRealCartRetainsNativeAmmoRules)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:archer", true));
	const battle::Unit * attacker = nullptr, *defender = nullptr, *cart = nullptr;
	for(const auto * unit : parent->battleGetAllUnits(false))
		if(unit->isAmmoCart()) cart = unit;
		else if(unit->unitSide() == BattleSide::ATTACKER) attacker = unit;
		else defender = unit;
	ASSERT_TRUE(attacker && defender && cart);
	const auto id = attacker->unitId();
	auto original = parent->getForUpdate(id);
	original->setPosition(BattleHex(2, 5));
	int64_t loss = original->getAvailableHealth() - 2 * original->getMaxHealth();
	original->damage(loss, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	original->addUnitBonus(newHorizonsFocusMagic::enchantment(*original,
		SpellID(SpellID::decode(newHorizonsFocusMagic::SPELL))));
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *child);
	auto actor = child->getForUpdate(id);
	const auto shots = actor->shots.available();
	for(int remaining = 2; remaining >= 0; --remaining)
	{
		const auto action = acceptedPreview(child, id, defender->unitId(), true);
		forecast::NativeAdapter::replayAcceptedAttackNative(*reference, actor.get(), action, forecast::AcceptedAttackRoute::BestAction);
		ASSERT_TRUE(forecast::NativeAdapter::applyAcceptedAttack(*child, actor.get(), action, forecast::AcceptedAttackRoute::BestAction));
		EXPECT_EQ(actor->shots.available(), remaining > 0 ? shots : shots - 1);
		EXPECT_EQ(newHorizonsFocusMagic::charges(*actor), remaining);
		expectSame(*child, *reference);
		if(remaining == 1)
		{
			auto ammoCart = child->getForUpdate(cart->unitId());
			int64_t damage = ammoCart->getAvailableHealth(), expected = damage;
			reference->getForUpdate(cart->unitId())->damage(expected, false, battle::DamageProvenance::PHYSICAL_CREATURE);
			ASSERT_TRUE(forecast::NativeAdapter::damage(*child, *ammoCart, damage,
				battle::DamageProvenance::PHYSICAL_CREATURE, nullptr));
			ASSERT_TRUE(actor->shots.isLimited()); // Real Cart death affects next final Focus shot.
		}
	}
	EXPECT_TRUE(actor->isShooter()); // Native role survives the third enhanced shot.
	EXPECT_TRUE(child->forecastState()->unit(id)->focus->records.empty());
}
TEST_F(ForecastNativeAdapterTest, AcceptedPreviewRejectsStaleStampForeignPointersAndBudgetWithoutPublishing)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	const auto ids = parent->forecastState()->ids();
	const auto action = acceptedPreview(parent, ids[0], ids[1], false);
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *child);
	auto actor = child->getForUpdate(ids[0]);
	auto victim = child->getForUpdate(ids[1]);
	int64_t damage = 1;
	ASSERT_TRUE(forecast::NativeAdapter::damage(*child, *victim, damage, battle::DamageProvenance::PHYSICAL_CREATURE, nullptr));
	const auto before = actor->save(), victimBefore = victim->save();
	const auto revision = child->forecastState()->revision();
	const auto modelBefore = *child->forecastState();
	const auto mapBefore = child->stackStates;
	EXPECT_THROW(forecast::NativeAdapter::applyAcceptedAttack(*child, actor.get(), action,
		forecast::AcceptedAttackRoute::BestAction), std::logic_error);
	EXPECT_EQ(actor->save(), before);
	EXPECT_EQ(victim->save(), victimBefore);
	EXPECT_EQ(child->forecastState()->revision(), revision);
	EXPECT_TRUE(child->forecastState()->sameSnapshot(modelBefore));
	EXPECT_EQ(child->stackStates, mapBefore);
	const auto fresh = acceptedPreview(child, ids[0], ids[1], false);
	auto foreign = fresh;
	foreign.attack.attacker = parent->battleGetUnitByID(ids[0]);
	EXPECT_THROW(forecast::NativeAdapter::applyAcceptedAttack(*child, actor.get(), foreign,
		forecast::AcceptedAttackRoute::BestAction), std::logic_error);
	child->setSearchBudget(std::make_shared<battleSearch::SearchBudget>(battleSearch::WorkAmounts{}));
	EXPECT_THROW(forecast::NativeAdapter::applyAcceptedAttack(*child, actor.get(), fresh,
		forecast::AcceptedAttackRoute::BestAction), battleSearch::BudgetExhausted);
	EXPECT_EQ(actor->save(), before);
	EXPECT_EQ(victim->save(), victimBefore);
	EXPECT_EQ(child->forecastState()->revision(), revision);
	EXPECT_TRUE(child->forecastState()->sameSnapshot(modelBefore));
	EXPECT_EQ(child->stackStates, mapBefore);
}
TEST_F(ForecastNativeAdapterTest, PotentialTargetsOffersStampActualSourceAndBestChoiceMatchesReference)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto id = parent->battleGetAllUnits(false).front()->unitId();
	DamageCache cache;
	PotentialTargets compact(parent->battleGetUnitByID(id), cache, parent);
	ASSERT_NE(parent->forecastState(), nullptr); // Actual producer boundary admitted.
	ASSERT_FALSE(compact.possibleAttacks.empty());
	ASSERT_TRUE(compact.bestAction().forecastSource);
	EXPECT_TRUE(compact.bestAction().forecastSource->sameSnapshot(*parent->forecastState()));
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), visible);
	const auto & chosen = compact.bestAction();
	DamageCache referenceCache;
	const auto world = std::make_shared<const VisibleCandidateWorld>(reference);
	const auto native = VisibleCandidateWorld::evaluate(world, reference->battleGetUnitByID(id),
		reference->battleGetUnitByID(chosen.attack.defender->unitId()), chosen.attack.chargeDistance,
		chosen.attack.shooting, chosen.from, referenceCache);
	EXPECT_FLOAT_EQ(chosen.defenderDamageReduce, native.defenderDamageReduce);
	EXPECT_FLOAT_EQ(chosen.attackerDamageReduce, native.attackerDamageReduce);
	const auto actor = parent->getForUpdate(id);
	applyProjectedBestAction(*parent, actor.get(), compact.bestAction());
	EXPECT_EQ(parent->forecastState()->unit(id)->health.available(), actor->getAvailableHealth());
}

TEST_F(ForecastNativeAdapterTest, LateAcceptedPlanBudgetFailureRestoresLazyMapCachesModelAndObservation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto sourceActor = parent->getForUpdate(parent->battleGetAllUnits(false).front()->unitId());
	sourceActor->setPosition(BattleHex(2, 5));
	sourceActor->addUnitBonus(newHorizonsFocusMagic::enchantment(*sourceActor,
		SpellID(SpellID::decode(newHorizonsFocusMagic::SPELL))));
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	const auto ids = parent->forecastState()->ids();
	const auto action = acceptedPreview(parent, ids[0], ids[1], true);
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *child);
	ASSERT_TRUE(child->stackStates.empty());
	// Measure the actual canonical prospective constructor on this SAME source,
	// rather than guessing a quota that fails before any lazy target exists.
	const auto node = static_cast<size_t>(battleSearch::WorkKind::Node);
	battleSearch::WorkAmounts ample{};
	ample[node] = 100000;
	auto measurement = std::make_shared<battleSearch::SearchBudget>(ample);
	{
		auto borrowed = std::shared_ptr<CBattleInfoCallback>(child.get(), [](CBattleInfoCallback *) {});
		HypotheticBattle probe(child->env, std::move(borrowed),
			std::shared_ptr<const battle::OwnedBattleFrameBatch>{}, measurement);
		EXPECT_EQ(probe.getBattleID(), child->getBattleID());
	}
	const auto constructorWork = measurement->spent()[node];
	ASSERT_GT(constructorWork, 0u);
	ASSERT_EQ(measurement->refusals()[node], 0u);
	ASSERT_TRUE(child->stackStates.empty());
	const auto before = *child->forecastState();
	const auto mapBefore = child->stackStates;
	const auto * inherited = child->battleGetUnitByID(ids[0]);
	const auto actorBefore = inherited->acquireState()->save();
	const auto defenderBefore = child->battleGetUnitByID(ids[1])->acquireState()->save();
	const auto spellsBefore = child->getLocalSpellEffects(ids[0]);
	ASSERT_FALSE(spellsBefore.empty());
	DamageCache cache;
	cache.buildDamageCache(parent, inherited->unitSide());
	const auto cacheBefore = cache.captureSnapshot();
	const auto rng = projectedHitTest::serialized(*handler->randomizer);
	const auto ambient = projectedHitTest::serialized(CRandomGenerator::getDefault());
	ASSERT_TRUE(child->beginLifeDrainFriendlyObservation(ids[0]));
	battleSearch::WorkAmounts quota{};
	quota[node] = constructorWork + static_cast<uint32_t>(ids.size() * (ids.size() + 1));
	auto work = std::make_shared<battleSearch::SearchBudget>(quota);
	child->setSearchBudget(work);
	// This exact quota completes the ctor and every lazy materialization, then
	// rejects the first prepareEffectApplication debit after the native replay.
	EXPECT_THROW(forecast::NativeAdapter::applyAcceptedAttack(*child, inherited, action,
		forecast::AcceptedAttackRoute::BestAction), battleSearch::BudgetExhausted);
	EXPECT_EQ(work->spent()[node], quota[node]);
	EXPECT_EQ(work->refusals()[node], 1u);
	EXPECT_EQ(child->stackStates, mapBefore);
	EXPECT_EQ(child->battleGetUnitByID(ids[0]), inherited);
	EXPECT_TRUE(child->forecastState()->sameSnapshot(before));
	EXPECT_EQ(inherited->acquireState()->save(), actorBefore);
	EXPECT_EQ(child->battleGetUnitByID(ids[1])->acquireState()->save(), defenderBefore);
	const auto spellsAfter = child->getLocalSpellEffects(ids[0]);
	ASSERT_EQ(spellsAfter.size(), spellsBefore.size());
	for(size_t index = 0; index < spellsBefore.size(); ++index)
		EXPECT_TRUE(battle::exactBattleEffectEqual(spellsBefore[index], spellsAfter[index]));
	EXPECT_EQ(newHorizonsFocusMagic::charges(*inherited), 3);
	EXPECT_TRUE(cache.matchesSnapshot(cacheBefore));
	EXPECT_TRUE(child->finishLifeDrainFriendlyObservation());
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), rng);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), ambient);
}
TEST_F(ForecastNativeAdapterTest, AcceptedPreviewRejectsChangedMutableQueryDespiteUnchangedDestinationStamp)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	const auto ids = parent->forecastState()->ids();
	const auto action = acceptedPreview(parent, ids[0], ids[1], false);
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *child);
	const auto before = *child->forecastState();
	ASSERT_TRUE(action.forecastSource->sameSnapshot(before));
	int64_t damage = 1;
	auto changed = parent->getForUpdate(ids[1]);
	ASSERT_TRUE(forecast::NativeAdapter::damage(*parent, *changed, damage,
		battle::DamageProvenance::PHYSICAL_CREATURE, nullptr));
	ASSERT_FALSE(action.forecastSource->sameSnapshot(*parent->forecastState()));
	const auto mapBefore = child->stackStates;
	EXPECT_THROW(forecast::NativeAdapter::applyAcceptedAttack(*child, parent->battleGetUnitByID(ids[0]), action,
		forecast::AcceptedAttackRoute::BestAction), std::logic_error);
	EXPECT_TRUE(child->forecastState()->sameSnapshot(before));
	EXPECT_EQ(child->stackStates, mapBefore);
}
TEST_F(ForecastNativeAdapterTest, AcceptedAttackRetainsEarlierBloodrageDeathMembershipAndSiblingCredit)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:pikeman", false, false, "core:cragHack"));
	ASSERT_EQ(parent->getBloodrageRank(BattleSide::ATTACKER), MasteryLevel::BASIC);
	uint32_t actorId = 0, priorId = 0, defenderId = 0;
	for(const auto * unit : parent->battleGetAllUnits(false))
	{
		if(unit->unitSide() == BattleSide::DEFENDER) defenderId = unit->unitId();
		else if(unit->creatureId() == CreatureID(CreatureID::decode("core:pikeman"))) actorId = unit->unitId();
		else if(unit->creatureId() == CreatureID(CreatureID::decode("core:airElemental"))) priorId = unit->unitId();
	}
	ASSERT_NE(actorId, priorId);
	auto actor = parent->getForUpdate(actorId);
	actor->setPosition(BattleHex(2, 5));
	actor->addUnitBonus(newHorizonsFocusMagic::enchantment(*actor,
		SpellID(SpellID::decode(newHorizonsFocusMagic::SPELL))));
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *child);
	actor = child->getForUpdate(actorId);
	auto prior = child->getForUpdate(priorId);
	const auto initialRage = child->getBloodrageDamagePercent(BattleSide::ATTACKER);
	int64_t lethal = prior->getAvailableHealth();
	ASSERT_TRUE(forecast::NativeAdapter::damage(*child, *prior, lethal,
		battle::DamageProvenance::PHYSICAL_CREATURE, nullptr));
	ASSERT_FALSE(prior->alive());
	child->recordBloodrageTransition(prior, true); // Actual native once-per-death owner.
	const auto earned = child->getBloodrageDamagePercent(BattleSide::ATTACKER);
	ASSERT_GT(earned, initialRage);
	child->recordBloodrageTransition(prior, true);
	ASSERT_EQ(child->getBloodrageDamagePercent(BattleSide::ATTACKER), earned);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto siblingActor = sibling->battleGetUnitByID(actorId)->acquireState()->save();
	const auto siblingRage = sibling->getBloodrageDamagePercent(BattleSide::ATTACKER);
	const auto action = acceptedPreview(child, actorId, defenderId, true);
	ASSERT_TRUE(forecast::NativeAdapter::applyAcceptedAttack(*child, actor.get(), action,
		forecast::AcceptedAttackRoute::BestAction));
	ASSERT_FALSE(prior->alive());
	EXPECT_EQ(child->getBloodrageDamagePercent(BattleSide::ATTACKER), earned);
	// An unchanged dead stack must not get credited a second time after AP.
	// Without seeding the prospective set this increments rage again.
	child->recordBloodrageTransition(prior, true);
	EXPECT_EQ(child->getBloodrageDamagePercent(BattleSide::ATTACKER), earned);
	EXPECT_EQ(parent->getBloodrageDamagePercent(BattleSide::ATTACKER), initialRage);
	EXPECT_TRUE(parent->battleGetUnitByID(priorId)->alive());
	EXPECT_EQ(newHorizonsFocusMagic::charges(*parent->battleGetUnitByID(actorId)), 3);
	EXPECT_EQ(sibling->getBloodrageDamagePercent(BattleSide::ATTACKER), siblingRage);
	EXPECT_EQ(sibling->battleGetUnitByID(actorId)->acquireState()->save(), siblingActor);
}
