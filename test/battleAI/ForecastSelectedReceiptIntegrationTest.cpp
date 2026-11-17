/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapterFixture.h"
#include "../../AI/BattleAI/ForecastNativeReadView.h"
#include "../../AI/BattleAI/ForecastAcceptedSnapshotSeal.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../lib/battle/BattleEffectExchange.h"
#include <stdexcept>
namespace
{
const battle::Unit * selectedRole(const std::shared_ptr<HypotheticBattle> & source, BattleSide side)
{
	const auto units = source->battleGetUnitsIf([side](const battle::Unit * unit)
	{ return unit->unitSide() == side && unit->unitSlot() == SlotID(0); });
	if(units.size() != 1) throw std::logic_error("Selected fixture requires exactly one unit in its side's first slot");
	return units.front();
}
AttackPossibility selectedPreview(const std::shared_ptr<HypotheticBattle> & source, bool ranged, BattleHex from)
{
	DamageCache cache; cache.buildDamageCache(source, BattleSide::ATTACKER);
	const auto world = std::make_shared<const VisibleCandidateWorld>(source);
	return VisibleCandidateWorld::evaluate(world, selectedRole(source, BattleSide::ATTACKER),
		selectedRole(source, BattleSide::DEFENDER), 0, ranged, from, cache);
}
void fullAgreement(const HypotheticBattle & actual, HypotheticBattle & native)
{
	for(const auto id : actual.forecastState()->orderedIds())
	{
		const auto * unit = actual.battleGetUnitByID(id);
		auto expected = native.getForUpdate(id);
		EXPECT_EQ(unit->acquireState()->save(), expected->acquireState()->save());
		const auto reader = actual.forecastState()->readView(id);
		ASSERT_TRUE(reader);
		EXPECT_EQ(reader->unit().acquireState()->save(), expected->acquireState()->save());
		EXPECT_EQ(actual.forecastState()->unit(id)->nativeValues->status().count, unit->getCount());
		EXPECT_EQ(newHorizonsFocusMagic::charges(*unit), newHorizonsFocusMagic::charges(*expected));
	}
}
}
TEST_F(ForecastNativeAdapterTest, SelectedMovementActualEntryMatchesSharedNativeAuthorityAndCompleteReaders)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto attackers = parent->battleGetUnitsIf([](const battle::Unit * unit)
	{ return unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0); });
	const auto defenders = parent->battleGetUnitsIf([](const battle::Unit * unit)
	{ return unit->unitSide() == BattleSide::DEFENDER && unit->unitSlot() == SlotID(0); });
	ASSERT_EQ(attackers.size(), 1u); ASSERT_EQ(defenders.size(), 1u);
	const auto id = attackers.front()->unitId(), targetId = defenders.front()->unitId();
	parent->getForUpdate(id)->setPosition(BattleHex(2, 5));
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	const auto action = selectedPreview(parent, false, BattleHex(6, 5));
	ASSERT_NE(action.attack.attacker, nullptr); ASSERT_NE(action.attack.defender, nullptr);
	ASSERT_EQ(action.attack.attacker->unitId(), id);
	ASSERT_EQ(action.attack.defender->unitId(), targetId);
	ASSERT_TRUE(action.forecastSource);
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto native = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *child);
	forecast::NativeAdapter::forkExchange(*parent, *sibling);
	auto actor = child->getForUpdate(id), expected = native->getForUpdate(id);
	const auto * stable = actor.get();
	const auto parentBefore = parent->battleGetUnitByID(id)->acquireState()->save();
	const auto rng = projectedHitTest::serialized(*handler->randomizer);
	const auto ambient = projectedHitTest::serialized(CRandomGenerator::getDefault());
	const auto expectedOrigin = selectedAttackReceipt::replayMovementNative(action, *expected, *native);
	const auto origin = selectedAttackReceipt::commitMovement(action, *actor, *child); // Actual selected entry.
	EXPECT_EQ(origin, expectedOrigin); EXPECT_EQ(actor->getPosition(), BattleHex(6, 5));
	EXPECT_EQ(child->battleGetUnitByID(id), stable);
	fullAgreement(*child, *native);
	EXPECT_EQ(parent->battleGetUnitByID(id)->acquireState()->save(), parentBefore);
	EXPECT_EQ(sibling->battleGetUnitByID(id)->acquireState()->save(), parentBefore);
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), rng);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), ambient);
}
TEST_F(ForecastNativeAdapterTest, SelectedResourceActualEntryPreservesNativeFieldsHpAndAcceptedFocusWithoutDoublePayment)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:archer"));
	const auto attackers = parent->battleGetUnitsIf([](const battle::Unit * unit)
	{ return unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0); });
	const auto defenders = parent->battleGetUnitsIf([](const battle::Unit * unit)
	{ return unit->unitSide() == BattleSide::DEFENDER && unit->unitSlot() == SlotID(0); });
	ASSERT_EQ(attackers.size(), 1u); ASSERT_EQ(defenders.size(), 1u);
	const auto id = attackers.front()->unitId(), targetId = defenders.front()->unitId();
	auto root = parent->getForUpdate(id); root->setPosition(BattleHex(2, 5));
	root->addUnitBonus(newHorizonsFocusMagic::enchantment(*root, SpellID(SpellID::decode(newHorizonsFocusMagic::SPELL))));
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto action = selectedPreview(parent, true, BattleHex(2, 5));
	ASSERT_NE(action.attack.attacker, nullptr); ASSERT_NE(action.attack.defender, nullptr);
	ASSERT_EQ(action.attack.attacker->unitId(), id);
	ASSERT_EQ(action.attack.defender->unitId(), targetId);
	ASSERT_TRUE(action.forecastSource); ASSERT_TRUE(action.attackerState);
	action.attackerState->archeryCrossfireAttackers = {21, 22};
	action.attackerState->archeryCrossfireDefenders = {31, 32};
	action.attackerState->archeryCounterfireRound = 5;
	action.attackerState->forestAmbush.attacked = true;
	action.attackerState->rangedFollowUpDamagePercent = 40;
	ASSERT_TRUE(action.snapshotSeal && action.snapshotSeal->matches(action)); // Identity does not freeze mutable contents.
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto native = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *child);
	auto actor = child->getForUpdate(id), expected = native->getForUpdate(id);
	int64_t damage = 1, nativeDamage = damage;
	ASSERT_TRUE(forecast::NativeAdapter::damage(*child, *actor, damage, battle::DamageProvenance::SPELL, nullptr));
	expected->damage(nativeDamage, false, battle::DamageProvenance::SPELL);
	const auto health = actor->getAvailableHealth();
	const auto * provider = actor->getBonusBearer();
	const auto * stable = actor.get();
	focusMagicAI::restoreAcceptedRecords(*expected, *action.attackerState);
	selectedAttackReceipt::replayResourcesNative(action, *action.attackerState, expected, expected, *native);
	selectedAttackReceipt::restoreResources(action, *action.attackerState, actor, actor, *child); // Actual entry.
	EXPECT_EQ(child->battleGetUnitByID(id), stable); EXPECT_EQ(actor->getBonusBearer(), provider);
	EXPECT_EQ(actor->getAvailableHealth(), health); EXPECT_EQ(actor->rangedFollowUpDamagePercent, 0);
	EXPECT_EQ(actor->archeryCrossfireAttackers, action.attackerState->archeryCrossfireAttackers);
	EXPECT_EQ(actor->shots.available(), expected->shots.available());
	EXPECT_EQ(newHorizonsFocusMagic::charges(*actor), newHorizonsFocusMagic::charges(*action.attackerState));
	fullAgreement(*child, *native);
}
TEST_F(ForecastNativeAdapterTest, SelectedReceiptRejectsForeignSnapshotAndChangedQueryBeforeScalarPrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	const auto id = selectedRole(parent, BattleSide::ATTACKER)->unitId();
	const auto action = selectedPreview(parent, false, parent->battleGetUnitByID(id)->getPosition());
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *child);
	auto actor = child->getForUpdate(id);
	const auto values = actor->save(); const auto model = *child->forecastState(); const auto map = child->stackStates;
	auto foreign = action.attackerState->acquireState(); // Same ID/profile; not the retained AP object.
	EXPECT_THROW(selectedAttackReceipt::restoreResources(action, *foreign, actor, actor, *child), std::logic_error);
	EXPECT_EQ(actor->save(), values); EXPECT_TRUE(child->forecastState()->sameSnapshot(model)); EXPECT_EQ(child->stackStates, map);
	int64_t damage = 1; auto changed = parent->getForUpdate(action.attack.defender->unitId());
	ASSERT_TRUE(forecast::NativeAdapter::damage(*parent, *changed, damage, battle::DamageProvenance::SPELL, nullptr));
	EXPECT_THROW(forecast::NativeAdapter::validateAcceptedSnapshot(*action.attackerState, action), std::logic_error);
	EXPECT_THROW(selectedAttackReceipt::commitMovement(action, *actor, *child), std::logic_error);
	EXPECT_EQ(actor->save(), values); EXPECT_TRUE(child->forecastState()->sameSnapshot(model)); EXPECT_EQ(child->stackStates, map);
}
TEST_F(ForecastNativeAdapterTest, LateSelectedMovementBudgetRefusalRollsBackLazyPeersFlagsCacheAndObserver)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto attackers = parent->battleGetUnitsIf([](const battle::Unit * unit)
	{ return unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0); });
	const auto defenders = parent->battleGetUnitsIf([](const battle::Unit * unit)
	{ return unit->unitSide() == BattleSide::DEFENDER && unit->unitSlot() == SlotID(0); });
	ASSERT_EQ(attackers.size(), 1u); ASSERT_EQ(defenders.size(), 1u);
	const auto id = attackers.front()->unitId(), targetId = defenders.front()->unitId();
	parent->getForUpdate(id)->setPosition(BattleHex(2, 5));
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	const auto action = selectedPreview(parent, false, BattleHex(6, 5));
	ASSERT_NE(action.attack.attacker, nullptr); ASSERT_NE(action.attack.defender, nullptr);
	ASSERT_EQ(action.attack.attacker->unitId(), id);
	ASSERT_EQ(action.attack.defender->unitId(), targetId);
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *child);
	auto actor = child->getForUpdate(id);
	ASSERT_EQ(child->stackStates.size(), 1u); // One other peer is still lazy.
	const auto node = static_cast<size_t>(battleSearch::WorkKind::Node);
	battleSearch::WorkAmounts ample{}; ample[node] = 100000;
	auto measurement = std::make_shared<battleSearch::SearchBudget>(ample);
	{
		auto borrowed = std::shared_ptr<CBattleInfoCallback>(child.get(), [](CBattleInfoCallback *) {});
		HypotheticBattle probe(child->env, std::move(borrowed), std::shared_ptr<const battle::OwnedBattleFrameBatch>{}, measurement);
		EXPECT_EQ(probe.getBattleID(), child->getBattleID());
	}
	const auto constructorWork = measurement->spent()[node];
	ASSERT_GT(constructorWork, 0u); ASSERT_EQ(measurement->refusals()[node], 0u);
	const auto map = child->stackStates; const auto model = *child->forecastState();
	const auto values = actor->save(); const auto * stable = actor.get(); const auto * provider = actor->getBonusBearer();
	const auto sourceFacts = actor->ownedModifierFacts();
	DamageCache cache; cache.buildDamageCache(parent, BattleSide::ATTACKER); const auto damageCache = cache.captureSnapshot();
	const auto rng = projectedHitTest::serialized(*handler->randomizer);
	const auto ambient = projectedHitTest::serialized(CRandomGenerator::getDefault());
	ASSERT_TRUE(child->beginLifeDrainFriendlyObservation(id));
	battleSearch::WorkAmounts quota{}; const auto peers = child->forecastState()->ids().size();
	quota[node] = constructorWork + static_cast<uint32_t>(peers * (peers + 1));
	auto work = std::make_shared<battleSearch::SearchBudget>(quota); child->setSearchBudget(work);
	// Ctor + every lazy peer/draft + movement replay complete BEFORE the first
	// prepareEffectApplication debit. Refusal is late, never a ctor-only test.
	EXPECT_THROW(selectedAttackReceipt::commitMovement(action, *actor, *child), battleSearch::BudgetExhausted);
	EXPECT_EQ(work->spent()[node], quota[node]); EXPECT_EQ(work->refusals()[node], 1u);
	EXPECT_EQ(child->stackStates, map); EXPECT_TRUE(child->forecastState()->sameSnapshot(model));
	EXPECT_EQ(child->battleGetUnitByID(id), stable); EXPECT_EQ(actor->save(), values);
	EXPECT_EQ(actor->getBonusBearer(), provider); EXPECT_EQ(actor->ownedModifierFacts(), sourceFacts);
	EXPECT_TRUE(cache.matchesSnapshot(damageCache)); EXPECT_TRUE(child->finishLifeDrainFriendlyObservation());
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), rng);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), ambient);
}

TEST_F(ForecastNativeAdapterTest, CompactSelectedOverwatchRefusesBeforeAllScalarSideObstacleAndObserverChanges)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	const auto id = selectedRole(parent, BattleSide::ATTACKER)->unitId();
	auto action = selectedPreview(parent, false, parent->battleGetUnitByID(id)->getPosition());
	action.overwatchHits.push_back({id, BattleHex(6, 5), 1});
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *child);
	auto actor = child->getForUpdate(id);
	const auto map = child->stackStates; const auto model = *child->forecastState();
	std::vector<JsonNode> values;
	for(const auto * unit : child->battleGetAllUnits(false)) values.push_back(unit->acquireState()->save());
	const auto attackerSide = child->forecastSide(BattleSide::ATTACKER);
	const auto defenderSide = child->forecastSide(BattleSide::DEFENDER);
	const auto obstacles = child->getAllObstacles(); const auto obstacleChanges = child->hasObstacleChanges();
	const auto rng = projectedHitTest::serialized(*handler->randomizer);
	ASSERT_TRUE(child->beginLifeDrainFriendlyObservation(id));
	EXPECT_THROW(selectedAttackReceipt::commitMovement(action, *actor, *child), std::logic_error);
	EXPECT_THROW(selectedAttackReceipt::restoreResources(action, *action.attackerState, actor, actor, *child), std::logic_error);
	EXPECT_THROW(forecast::NativeAdapter::applyAcceptedAttack(*child, actor.get(), action,
		forecast::AcceptedAttackRoute::BestAction), std::logic_error);
	EXPECT_EQ(child->stackStates, map); EXPECT_TRUE(child->forecastState()->sameSnapshot(model));
	size_t index = 0;
	for(const auto * unit : child->battleGetAllUnits(false)) EXPECT_EQ(unit->acquireState()->save(), values.at(index++));
	EXPECT_EQ(child->forecastSide(BattleSide::ATTACKER), attackerSide);
	EXPECT_EQ(child->forecastSide(BattleSide::DEFENDER), defenderSide);
	EXPECT_EQ(child->getAllObstacles(), obstacles); EXPECT_EQ(child->hasObstacleChanges(), obstacleChanges);
	EXPECT_TRUE(child->finishLifeDrainFriendlyObservation());
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), rng);
}
TEST_F(ForecastNativeAdapterTest, AcceptedSnapshotProofRejectsSameIdOwnedForeignScoringAndBorrowedAncestors)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	const auto id = selectedRole(parent, BattleSide::ATTACKER)->unitId();
	const auto action = selectedPreview(parent, false, parent->battleGetUnitByID(id)->getPosition());
	EXPECT_NO_THROW(forecast::NativeAdapter::validateAcceptedSnapshot(*action.attackerState, action));
	auto foreign = std::make_shared<HypotheticBattle>(environment.get(), visible); // Independent capture: same numeric IDs.
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*foreign));
	auto foreignLoan = foreign->getForUpdate(id)->acquireState();
	ASSERT_TRUE(forecast::NativeReadView::querySource(*foreignLoan));
	auto foreignAction = action; foreignAction.attackerState = foreignLoan;
	EXPECT_FALSE(foreignAction.snapshotSeal->matches(foreignAction));
	EXPECT_THROW(forecast::NativeAdapter::validateAcceptedSnapshot(*foreignLoan, foreignAction), std::logic_error);
	auto scoringWorld = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto scoringUnit = scoringWorld->getForUpdate(id); scoringUnit->freezeDamageScoringBonusSnapshot();
	auto scoring = scoringUnit->acquireState();
	const auto scoringProof = forecast::NativeReadView::querySource(*scoring);
	ASSERT_TRUE(scoringProof && scoringProof->scoring);
	auto scoringAction = action; scoringAction.attackerState = scoring;
	EXPECT_FALSE(scoringAction.snapshotSeal->matches(scoringAction));
	EXPECT_THROW(forecast::NativeAdapter::validateAcceptedSnapshot(*scoring, scoringAction), std::logic_error);
	const auto & original = parent->forecastState()->readView(id)->unit();
	battle::CUnitStateDetached borrowed(&original, original.getBonusBearer());
	EXPECT_THROW(forecast::NativeAdapter::validateAcceptedSnapshot(borrowed, action), std::logic_error);
}

TEST_F(ForecastNativeAdapterTest, LateSelectedResourceBudgetRefusalRollsBackFocusVectorsAmmoAndAllOwnedState)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:archer"));
	const auto id = selectedRole(parent, BattleSide::ATTACKER)->unitId();
	auto root = parent->getForUpdate(id); root->setPosition(BattleHex(2, 5));
	root->addUnitBonus(newHorizonsFocusMagic::enchantment(*root, SpellID(SpellID::decode(newHorizonsFocusMagic::SPELL))));
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto action = selectedPreview(parent, true, BattleHex(2, 5));
	action.attackerState->archeryCrossfireAttackers = {11, 22, 33};
	action.attackerState->archeryCounterfireRound = 9;
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *child);
	auto actor = child->getForUpdate(id);
	const auto node = static_cast<size_t>(battleSearch::WorkKind::Node);
	battleSearch::WorkAmounts ample{}; ample[node] = 100000;
	auto measurement = std::make_shared<battleSearch::SearchBudget>(ample);
	{
		auto borrowed = std::shared_ptr<CBattleInfoCallback>(child.get(), [](CBattleInfoCallback *) {});
		HypotheticBattle probe(child->env, std::move(borrowed), std::shared_ptr<const battle::OwnedBattleFrameBatch>{}, measurement);
		EXPECT_EQ(probe.getBattleID(), child->getBattleID());
	}
	ASSERT_GT(measurement->spent()[node], 0u); ASSERT_EQ(measurement->refusals()[node], 0u);
	const auto model = *child->forecastState(); const auto map = child->stackStates;
	const auto values = actor->save(); const auto facts = actor->ownedModifierFacts();
	const auto shots = actor->shots.available(); const auto focus = newHorizonsFocusMagic::charges(*actor);
	const auto side = child->forecastSide(BattleSide::ATTACKER); const auto obstacles = child->getAllObstacles();
	const auto rng = projectedHitTest::serialized(*handler->randomizer);
	ASSERT_TRUE(child->beginLifeDrainFriendlyObservation(id));
	battleSearch::WorkAmounts quota{}; const auto peers = child->forecastState()->ids().size();
	quota[node] = measurement->spent()[node] + static_cast<uint32_t>(peers * (peers + 1));
	auto work = std::make_shared<battleSearch::SearchBudget>(quota); child->setSearchBudget(work);
	EXPECT_THROW(selectedAttackReceipt::restoreResources(action, *action.attackerState, actor, actor, *child), battleSearch::BudgetExhausted);
	EXPECT_EQ(work->spent()[node], quota[node]); EXPECT_EQ(work->refusals()[node], 1u);
	EXPECT_EQ(actor->save(), values); EXPECT_EQ(actor->shots.available(), shots);
	EXPECT_EQ(actor->ownedModifierFacts(), facts); EXPECT_EQ(newHorizonsFocusMagic::charges(*actor), focus);
	EXPECT_EQ(child->stackStates, map); EXPECT_TRUE(child->forecastState()->sameSnapshot(model));
	EXPECT_EQ(child->forecastSide(BattleSide::ATTACKER), side); EXPECT_EQ(child->getAllObstacles(), obstacles);
	EXPECT_TRUE(child->finishLifeDrainFriendlyObservation()); EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), rng);
}

TEST_F(ForecastNativeAdapterTest, ProducerSealRejectsSwappedSameProgramAncestorAndSiblingLoansAfterNonlethalDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	const auto id = selectedRole(parent, BattleSide::ATTACKER)->unitId();
	auto ancestor = parent->getForUpdate(id)->acquireState();
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *sibling);
	auto siblingLoan = sibling->getForUpdate(id)->acquireState();
	int64_t damage = 1; auto changed = parent->getForUpdate(id);
	const auto count = changed->getCount();
	ASSERT_TRUE(forecast::NativeAdapter::damage(*parent, *changed, damage, battle::DamageProvenance::SPELL, nullptr));
	ASSERT_EQ(changed->getCount(), count); // SAME programme across a scalar-only source generation change.
	const auto action = selectedPreview(parent, false, changed->getPosition());
	ASSERT_TRUE(action.snapshotSeal && action.snapshotSeal->matches(action));
	EXPECT_NO_THROW(forecast::NativeAdapter::validateAcceptedSnapshot(*action.attackerState, action));
	auto borrowedLease = action;
	borrowedLease.attackerState = std::shared_ptr<battle::CUnitState>(action.attackerState.get(), [](battle::CUnitState *) {});
	EXPECT_FALSE(borrowedLease.snapshotSeal->matches(borrowedLease)); // Same pointer, different ownership is not emitted identity.
	EXPECT_THROW(forecast::NativeAdapter::validateAcceptedSnapshot(*borrowedLease.attackerState, borrowedLease), std::logic_error);
	for(const auto & substituted : {ancestor, siblingLoan})
	{
		const auto proof = forecast::NativeReadView::querySource(*substituted);
		ASSERT_TRUE(proof && proof->modifiers);
		EXPECT_EQ(proof->modifiers, parent->forecastState()->unit(id)->currentModifiers); // Old programme check alone passes.
		auto forged = action; forged.attackerState = substituted;
		EXPECT_FALSE(forged.snapshotSeal->matches(forged));
		auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		forecast::NativeAdapter::forkExchange(*parent, *child);
		auto actor = child->getForUpdate(id);
		const auto values = actor->save(); const auto map = child->stackStates; const auto model = *child->forecastState();
		EXPECT_THROW(selectedAttackReceipt::restoreResources(forged, *substituted, actor, actor, *child), std::logic_error);
		EXPECT_THROW(forecast::NativeAdapter::applyAcceptedAttack(*child, actor.get(), forged,
			forecast::AcceptedAttackRoute::BestAction), std::logic_error);
		EXPECT_EQ(actor->save(), values); EXPECT_EQ(child->stackStates, map);
		EXPECT_TRUE(child->forecastState()->sameSnapshot(model));
	}
}
TEST_F(ForecastNativeAdapterTest, ForestAmbushRefreshUsesTrustedSealedProducerAndExactExistingAttackMetadata)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	const auto id = selectedRole(parent, BattleSide::ATTACKER)->unitId();
	auto preview = selectedPreview(parent, false, parent->battleGetUnitByID(id)->getPosition());
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *child);
	auto actor = child->getForUpdate(id);
	preview.attackerState->forestAmbush.attacked = true;
	selectedAttackReceipt::restoreResources(preview, *preview.attackerState, actor, actor, *child);
	ASSERT_NE(actor->forestAmbush, dynamic_cast<const battle::CUnitState *>(preview.attack.attacker)->forestAmbush);
	DamageCache cache; cache.buildDamageCache(child, BattleSide::ATTACKER);
	auto attack = preview.attack; attack.attacker = child->battleGetUnitByID(id);
	attack.defender = child->battleGetUnitByID(preview.attack.defender->unitId());
	const auto native = AttackPossibility::evaluate(attack, preview.from, cache, child, false, std::nullopt, nullptr,
		false, preview.validatedApproachOrigin); // Ordinary fixture has no validated Gnoll approach.
	auto refreshed = selectedAttackReceipt::refreshForestAmbush(preview, child, cache);
	ASSERT_TRUE(refreshed); ASSERT_TRUE(refreshed->snapshotSeal && refreshed->snapshotSeal->matches(*refreshed));
	EXPECT_TRUE(refreshed->forecastSource->sameSnapshot(*child->forecastState()));
	EXPECT_EQ(refreshed->queryWorld->callback(), child); EXPECT_EQ(refreshed->from, native.from);
	EXPECT_EQ(refreshed->attack.archeryRangedDamageMultiplierPercent, native.attack.archeryRangedDamageMultiplierPercent);
	EXPECT_EQ(refreshed->attackerState->save(), native.attackerState->save());
	EXPECT_NO_THROW(forecast::NativeAdapter::validateAcceptedSnapshot(*refreshed->attackerState, *refreshed));
}
