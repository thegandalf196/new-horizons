/*
 * InitialExportWorldTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "InitialExportWorldFixture.h"
#include "../../lib/CRandomGenerator.h"
#include <type_traits>

static_assert(!std::is_constructible_v<InitialExportWorld::ExportHandle, std::shared_ptr<Bonus>>);
static_assert(!std::is_convertible_v<InitialExportWorld, float>);

TEST_F(InitialExportWorldTest, NativeBattleWideAddOwnsValueMetadataAndEveryRecipientBeforeAnyNativeMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	const auto originalSave = incoming->acquireState()->save();
	const auto originalExports = incoming->getExportedBonusList().size();
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	auto world = std::make_unique<InitialExportWorld>(*battle());
	const auto origin = source(*world, *incoming), recipient = source(*world, *braced);
	const int oldAttack = world->unitAt(recipient.occurrence).getAttack(false);
	const int oldIncomingAttack = world->unitAt(origin.occurrence).getAttack(false);
	const int heroAttack = defenderSideHero->getPrimSkillLevel(PrimarySkill::ATTACK);
	Bonus metadata = record();
	const Bonus nativeMetadata(metadata);
	world->add(origin, metadata);
	metadata.val = 999; // The owned Bonus value cannot alias the originating value.
	EXPECT_EQ(world->unitAt(recipient.occurrence).getAttack(false), oldAttack + 4);
	EXPECT_EQ(world->unitAt(origin.occurrence).getAttack(false), oldIncomingAttack + 4);
	EXPECT_EQ(incoming->acquireState()->save(), originalSave);
	EXPECT_EQ(incoming->getExportedBonusList().size(), originalExports);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
	const auto frozen = freeze(*world);
	world.reset(); // Retire ALL providers before changing their borrowed origin.
	incoming->addNewBonus(std::make_shared<Bonus>(nativeMetadata));
	EXPECT_EQ(defenderSideHero->getPrimSkillLevel(PrimarySkill::ATTACK), heroAttack);
	ASSERT_NO_FATAL_FAILURE(compareNative(frozen));
}

TEST_F(InitialExportWorldTest, ArmyAttachmentIsSharedWithFriendlyCompanionButNotTheOpponent)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	auto * companion = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(72), 20);
	auto world = std::make_unique<InitialExportWorld>(*battle());
	const auto origin = source(*world, *incoming), friendly = source(*world, *companion), enemy = source(*world, *braced);
	const int friendBefore = world->unitAt(friendly.occurrence).getMovementRange(0), enemyBefore = world->unitAt(enemy.occurrence).getMovementRange(0);
	Bonus metadata = record(BonusDuration::ONE_BATTLE, BonusNodeType::ARMY);
	metadata.type = BonusType::STACKS_SPEED;
	metadata.subtype = BonusSubtypeID();
	metadata.val = 2; // ARMY A/D would be filtered at the hero-rating boundary.
	world->add(origin, metadata);
	EXPECT_EQ(world->unitAt(friendly.occurrence).getMovementRange(0), friendBefore + 2);
	EXPECT_EQ(world->unitAt(enemy.occurrence).getMovementRange(0), enemyBefore);
	const auto frozen = freeze(*world);
	world.reset();
	incoming->addNewBonus(std::make_shared<Bonus>(metadata));
	ASSERT_NO_FATAL_FAILURE(compareNative(frozen));
}

TEST_F(InitialExportWorldTest, CurrentPositionLimiterAndForkHaveIndependentCanonicalProviders)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	ASSERT_EQ(battle()->battleGetUnitByPos(BattleHex(94)), nullptr);
	auto world = std::make_unique<InitialExportWorld>(*battle());
	const auto origin = source(*world, *incoming), recipient = source(*world, *braced);
	Bonus metadata = record();
	metadata.limiter = std::make_shared<UnitOnHexLimiter>(BattleHexArray{BattleHex(94)});
	const int before = world->unitAt(recipient.occurrence).getAttack(false);
	world->add(origin, metadata);
	auto sibling = world->fork();
	EXPECT_EQ(world->unitAt(recipient.occurrence).getAttack(false), before);
	auto state = world->unitAt(recipient.occurrence).acquireState();
	state->setPosition(BattleHex(94));
	const auto moved = state->save();
	state.reset();
	EXPECT_EQ(world->updateUnit(recipient.unitId, moved, 0), battle::ProjectedBattleWorldArena::UpdateDecision::APPLY);
	EXPECT_EQ(world->unitAt(recipient.occurrence).getAttack(false), before + 4);
	EXPECT_EQ(sibling->unitAt(recipient.occurrence).getAttack(false), before);
	const auto stationary = freeze(*sibling), arrived = freeze(*world);
	sibling.reset();
	world.reset();
	incoming->addNewBonus(std::make_shared<Bonus>(metadata));
	ASSERT_NO_FATAL_FAILURE(compareNative(stationary));
	nativeUpdate(recipient.unitId, moved, 0);
	ASSERT_NO_FATAL_FAILURE(compareNative(arrived));
}

TEST_F(InitialExportWorldTest, PropagationUpdaterCapturesActualSourceCountAtItsOrderedAddNotFinalCount)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	auto parent = std::make_unique<InitialExportWorld>(*battle());
	const auto origin = source(*parent, *incoming), recipient = source(*parent, *braced);
	const int64_t damage = 10LL * incoming->getMaxHealth();
	const auto first = injured(*parent, origin.occurrence, damage);
	parent->updateUnit(origin.unitId, first, -damage);
	Bonus metadata = record();
	metadata.propagationUpdater = std::make_shared<TimesStackSizeUpdater>(0, 1000, 10, 1);
	const int base = parent->unitAt(recipient.occurrence).getAttack(false);
	parent->add(origin, metadata);
	EXPECT_EQ(parent->unitAt(recipient.occurrence).getAttack(false), base + 9);
	const auto second = injured(*parent, origin.occurrence, damage);
	parent->updateUnit(origin.unitId, second, -damage);
	auto continuation = parent->fork();
	parent.reset(); // Its provider is not borrowed by the replayed continuation.
	EXPECT_EQ(continuation->unitAt(origin.occurrence).getCount(), 80);
	EXPECT_EQ(continuation->unitAt(recipient.occurrence).getAttack(false), base + 9);
	const auto frozen = freeze(*continuation);
	continuation.reset();
	nativeUpdate(origin.unitId, first, -damage);
	incoming->addNewBonus(std::make_shared<Bonus>(metadata));
	nativeUpdate(origin.unitId, second, -damage);
	ASSERT_NO_FATAL_FAILURE(compareNative(frozen));
}

TEST_F(InitialExportWorldTest, ExportedExpiryAndExactRemovalUnpropagateWithoutRemovingAncestorExports)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	auto inherited = std::make_shared<Bonus>(BonusDuration::UNTIL_ATTACK, BonusType::STACKS_SPEED,
		BonusSource::OTHER, 1, BonusSourceID());
	defenderSideHero->addNewBonus(inherited);
	auto world = std::make_unique<InitialExportWorld>(*battle());
	const auto origin = source(*world, *incoming);
	const Bonus metadata = record(BonusDuration::UNTIL_ATTACK);
	const auto handle = world->add(origin, metadata);
	auto removed = world->fork();
	world->expire(origin, InitialExportWorld::Expiry::UNTIL_ATTACK);
	removed->remove(handle);
	const auto expiredOracle = freeze(*world), removedOracle = freeze(*removed);
	world.reset();
	removed.reset();
	auto native = std::make_shared<Bonus>(metadata);
	incoming->addNewBonus(native);
	incoming->removeBonusesRecursive(Bonus::UntilAttack);
	ASSERT_TRUE(vstd::contains(defenderSideHero->getExportedBonusList(), inherited));
	ASSERT_NO_FATAL_FAILURE(compareNative(expiredOracle));
	native = std::make_shared<Bonus>(metadata);
	incoming->addNewBonus(native);
	incoming->removeBonus(native);
	ASSERT_TRUE(vstd::contains(defenderSideHero->getExportedBonusList(), inherited));
	ASSERT_NO_FATAL_FAILURE(compareNative(removedOracle));
}

TEST_F(InitialExportWorldTest, SiblingNewHandleCannotRemoveAnEqualValuedExportInAnotherBranch)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	auto parent = std::make_unique<InitialExportWorld>(*battle());
	const auto origin = source(*parent, *incoming);
	auto left = parent->fork(), right = parent->fork();
	const auto foreign = left->add(origin, record());
	right->add(origin, record());
	EXPECT_THROW(right->remove(foreign), std::invalid_argument);
	EXPECT_THROW(right->snapshotAt(origin.occurrence), std::logic_error);
	EXPECT_NO_THROW(left->snapshotAt(origin.occurrence));
	EXPECT_NO_THROW(parent->snapshotAt(origin.occurrence));
}

TEST_F(InitialExportWorldTest, EffectiveOrCopiedRecordWithoutOriginalExportIdentityIsNotGuessed)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	auto world = std::make_unique<InitialExportWorld>(*battle());
	const auto original = incoming->acquireState()->save();
	EXPECT_THROW(world->remove(InitialExportWorld::ExportHandle{}), std::invalid_argument);
	EXPECT_THROW(world->fork(), std::logic_error);
	EXPECT_EQ(incoming->acquireState()->save(), original);
}

TEST_F(InitialExportWorldTest, SourceOccurrenceRequiresTheExactOriginalNodeNotOnlyAnEqualIdOrValue)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	auto world = std::make_unique<InitialExportWorld>(*battle());
	auto ambiguous = source(*world, *incoming);
	ambiguous.original = braced;
	const auto count = incoming->getExportedBonusList().size();
	EXPECT_THROW(world->add(ambiguous, record()), std::invalid_argument);
	EXPECT_EQ(incoming->getExportedBonusList().size(), count);
	EXPECT_THROW(world->size(), std::logic_error);
}

TEST_F(InitialExportWorldTest, AnUnorderedEffectiveAddWithoutOriginCannotInventAnExportOwner)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	auto world = std::make_unique<InitialExportWorld>(*battle());
	const Bonus copiedEffectiveValue = record();
	const auto nativeCount = incoming->getExportedBonusList().size();
	EXPECT_THROW(world->add(InitialExportWorld::Source{}, copiedEffectiveValue), std::invalid_argument);
	EXPECT_THROW(world->fork(), std::logic_error);
	EXPECT_EQ(incoming->getExportedBonusList().size(), nativeCount);
}

TEST_F(InitialExportWorldTest, RecordedResourceSnapshotPrecedesVictimExpiryAcrossAnOwnedContinuation)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	auto world = std::make_unique<InitialExportWorld>(*battle());
	const auto origin = source(*world, *incoming), actor = source(*world, *braced);
	Bonus metadata = record(BonusDuration::UNTIL_BEING_ATTACKED);
	metadata.type = BonusType::UNLIMITED_RETALIATIONS;
	metadata.subtype = BonusSubtypeID();
	metadata.val = 1;
	world->add(origin, metadata);
	ASSERT_FALSE(world->unitAt(actor.occurrence).counterAttacks.isLimited());
	auto actorState = world->unitAt(actor.occurrence).acquireState();
	actorState->afterAttack(false, true, true);
	const auto afterAttack = actorState->save();
	actorState.reset();
	world->updateUnit(actor.unitId, afterAttack, 0);
	const auto afterInjury = injured(*world, origin.occurrence, 1);
	world->updateUnit(origin.unitId, afterInjury, -1);
	auto continuation = world->fork();
	world.reset();
	EXPECT_TRUE(continuation->unitAt(actor.occurrence).counterAttacks.isLimited());
	EXPECT_EQ(continuation->unitAt(actor.occurrence).counterAttacks.available(), 1);
	EXPECT_EQ(continuation->snapshotAt(actor.occurrence)["state"]["counterAttacks"]["used"].Integer(), 0);
	const auto frozen = freeze(*continuation);
	continuation.reset();
	incoming->addNewBonus(std::make_shared<Bonus>(metadata));
	nativeUpdate(actor.unitId, afterAttack, 0);
	nativeUpdate(origin.unitId, afterInjury, -1);
	ASSERT_NO_FATAL_FAILURE(compareNative(frozen));
}

TEST_F(InitialExportWorldTest, CanonicalDamageSnapshotLeavesParentSiblingNativeAndExpiryUntouched)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	auto world = std::make_unique<InitialExportWorld>(*battle());
	const auto origin = source(*world, *incoming), recipient = source(*world, *braced);
	const auto nativeBefore = incoming->acquireState()->save();
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	const Bonus metadata = record(BonusDuration::UNTIL_BEING_ATTACKED);
	const int base = world->unitAt(recipient.occurrence).getAttack(false);
	world->add(origin, metadata);
	auto sibling = world->fork();
	const auto before = world->snapshotAt(origin.occurrence);
	int64_t damage = incoming->getMaxHealth();
	const auto injuredState = world->snapshotAfterPhysicalDamage(origin, damage);
	EXPECT_EQ(damage, incoming->getMaxHealth());
	EXPECT_EQ(world->snapshotAt(origin.occurrence), before);
	EXPECT_EQ(sibling->snapshotAt(origin.occurrence), before);
	EXPECT_EQ(world->unitAt(recipient.occurrence).getAttack(false), base + 4);
	EXPECT_EQ(sibling->unitAt(recipient.occurrence).getAttack(false), base + 4);
	EXPECT_EQ(incoming->acquireState()->save(), nativeBefore);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
	auto continuation = world->fork();
	ASSERT_EQ(continuation->updateUnit(origin.unitId, injuredState, -damage),
		battle::ProjectedBattleWorldArena::UpdateDecision::APPLY);
	EXPECT_EQ(continuation->unitAt(origin.occurrence).getCount(), 99);
	EXPECT_EQ(continuation->unitAt(recipient.occurrence).getAttack(false), base);
	EXPECT_EQ(world->unitAt(recipient.occurrence).getAttack(false), base + 4);
	const auto frozen = freeze(*continuation);
	continuation.reset();
	sibling.reset();
	world.reset();
	incoming->addNewBonus(std::make_shared<Bonus>(metadata));
	nativeUpdate(origin.unitId, injuredState, -damage);
	ASSERT_NO_FATAL_FAILURE(compareNative(frozen));
}

TEST_F(InitialExportWorldTest, CanonicalDamageSnapshotPreservesGuardianAbsorptionAndRejectsForeignSource)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	InitialExportWorld world(*battle());
	const auto origin = source(world, *incoming);
	auto state = world.unitAt(origin.occurrence).acquireState();
	state->guardianSpiritHitPoints = 50;
	state->guardianSpiritRoundsRemaining = 2;
	world.copyQueryValues(origin, *state);
	state.reset();
	world.recordQuerySnapshot(origin);
	auto sibling = world.fork();
	const auto before = world.snapshotAt(origin.occurrence);
	int64_t damage = 20;
	const auto after = world.snapshotAfterPhysicalDamage(origin, damage);
	EXPECT_EQ(damage, 0);
	EXPECT_EQ(world.snapshotAt(origin.occurrence), before);
	EXPECT_EQ(sibling->snapshotAt(origin.occurrence), before);
	EXPECT_EQ(incoming->guardianSpiritHitPoints, 0);
	auto foreign = origin;
	foreign.original = braced;
	int64_t unchangedDamage = 17;
	EXPECT_THROW(world.snapshotAfterPhysicalDamage(foreign, unchangedDamage), std::invalid_argument);
	EXPECT_EQ(unchangedDamage, 17);
	EXPECT_EQ(world.snapshotAt(origin.occurrence), before);
	auto continuation = world.fork();
	continuation->updateUnit(origin.unitId, after, 0);
	EXPECT_EQ(continuation->unitAt(origin.occurrence).guardianSpiritHitPoints, 30);
	EXPECT_EQ(continuation->unitAt(origin.occurrence).getAvailableHealth(), incoming->getAvailableHealth());
}

TEST_F(InitialExportWorldTest, CurrentHeroGraphQueryKeepsOriginAndRemovalBranchLocal)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	auto world = std::make_unique<InitialExportWorld>(*battle());
	const auto origin = source(*world, *incoming);
	Bonus metadata = record();
	metadata.type = BonusType::HERO_GRANTS_ATTACKS;
	metadata.subtype = BonusSubtypeID();
	metadata.limiter.reset();
	const auto handle = world->add(origin, metadata);
	const auto held = world->projectedHeroBonuses(BattleSide::ATTACKER, true);
	const auto matching = Selector::type()(BonusType::HERO_GRANTS_ATTACKS)
		.And(Selector::sourceType()(BonusSource::OTHER));
	ASSERT_NE(held->getFirst(matching), nullptr);
	auto sibling = world->fork();
	world->remove(handle);
	EXPECT_EQ(world->projectedHeroBonuses(BattleSide::ATTACKER, true)->getFirst(matching), nullptr);
	EXPECT_NE(sibling->projectedHeroBonuses(BattleSide::ATTACKER, true)->getFirst(matching), nullptr);
	EXPECT_NE(held->getFirst(matching), nullptr);
	EXPECT_THROW(world->projectedHeroBonuses(BattleSide::NONE, true), std::invalid_argument);
	EXPECT_NO_THROW(world->size());
	const auto expected = projectedHitTest::serialized(*held->getFirst(matching));
	sibling.reset();
	world.reset();
	incoming->addNewBonus(std::make_shared<Bonus>(metadata));
	const auto native = attackerSideHero->getUnstackedBonuses(matching);
	ASSERT_EQ(native->size(), 1);
	EXPECT_EQ(projectedHitTest::serialized(*native->front()), expected);
}

TEST_F(InitialExportWorldTest, ConfiguredCohesionAndPainCrossStrictHalfOnlyInCanonicalScratchContinuation)
{
	ASSERT_NO_FATAL_FAILURE(preparePositiveDamageProfile());
	auto world = std::make_unique<InitialExportWorld>(*battle());
	const auto origin = source(*world, *incoming);
	auto sibling = world->fork(), continuation = world->fork();
	const auto original = world->snapshotAt(origin.occurrence);
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	const int64_t half = incoming->getAvailableHealth() / 2;
	int64_t firstDamage = half;
	const auto first = continuation->snapshotAfterPhysicalDamage(origin, firstDamage);
	ASSERT_EQ(firstDamage, half);
	EXPECT_FALSE(first["state"]["veteranCohesionEarned"].Bool());
	EXPECT_EQ(first["state"]["personalBloodrageIncrement"].Integer(), 0);
	continuation->updateUnit(origin.unitId, first, -firstDamage);
	int64_t secondDamage = 1;
	const auto second = continuation->snapshotAfterPhysicalDamage(origin, secondDamage);
	ASSERT_EQ(secondDamage, 1);
	EXPECT_TRUE(second["state"]["veteranCohesionEarned"].Bool());
	EXPECT_EQ(second["state"]["personalBloodrageIncrement"].Integer(), newHorizonsBloodrage::BASIC_INCREMENT);
	continuation->updateUnit(origin.unitId, second, -secondDamage);
	EXPECT_EQ(continuation->unitAt(origin.occurrence).getAvailableHealth(), half - 1);
	EXPECT_EQ(world->snapshotAt(origin.occurrence), original);
	EXPECT_EQ(sibling->snapshotAt(origin.occurrence), original);
	EXPECT_EQ(incoming->acquireState()->save(), original);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
	const auto frozen = freeze(*continuation);
	continuation.reset();
	sibling.reset();
	world.reset();
	auto native = incoming->acquireState();
	int64_t nativeDamage = half;
	native->damage(nativeDamage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(native->save(), first);
	native.reset();
	nativeUpdate(origin.unitId, first, -nativeDamage);
	native = incoming->acquireState();
	nativeDamage = 1;
	native->damage(nativeDamage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(native->save(), second);
	native.reset();
	nativeUpdate(origin.unitId, second, -nativeDamage);
	EXPECT_TRUE(incoming->veteranCohesionEarned);
	EXPECT_EQ(incoming->personalBloodrageIncrement, newHorizonsBloodrage::BASIC_INCREMENT);
	ASSERT_NO_FATAL_FAILURE(compareNative(frozen));
}

TEST_F(InitialExportWorldTest, DamageScratchRequiresCopiedValuesRecordedBeforeDependentOperations)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	InitialExportWorld world(*battle());
	const auto origin = source(world, *incoming);
	auto state = world.unitAt(origin.occurrence).acquireState();
	state->setPosition(BattleHex(94));
	world.copyQueryValues(origin, *state);
	state.reset();
	const auto copied = world.snapshotAt(origin.occurrence);
	int64_t damage = 1;
	EXPECT_THROW(world.snapshotAfterPhysicalDamage(origin, damage), std::logic_error);
	EXPECT_EQ(damage, 1);
	EXPECT_EQ(world.snapshotAt(origin.occurrence), copied);
	world.recordQuerySnapshot(origin);
	EXPECT_NO_THROW(world.snapshotAfterPhysicalDamage(origin, damage));
	auto values = battle::ProjectedUnitEnvironmentState::capture(*battle());
	values.state(incoming->unitSide()).fortune.speedUnits.insert(origin.unitId);
	const int speed = world.unitAt(origin.occurrence).getMovementRange(0);
	world.copyEnvironmentValues(values);
	EXPECT_EQ(world.unitAt(origin.occurrence).getMovementRange(0), speed + 2);
	damage = 1;
	EXPECT_THROW(world.snapshotAfterPhysicalDamage(origin, damage), std::logic_error);
	EXPECT_EQ(damage, 1);
	world.recordEnvironmentSnapshot(values);
	EXPECT_NO_THROW(world.snapshotAfterPhysicalDamage(origin, damage));
	EXPECT_EQ(world.fork()->unitAt(origin.occurrence).getMovementRange(0), speed + 2);
	world.copyEnvironmentValues(values);
	world.add(origin, record()); // Later snapshots cannot repair an earlier unrecorded dependency.
	world.recordEnvironmentSnapshot(values);
	damage = 1;
	EXPECT_THROW(world.snapshotAfterPhysicalDamage(origin, damage), std::logic_error);
	EXPECT_EQ(damage, 1);
	EXPECT_EQ(world.snapshotAt(origin.occurrence), copied);
}

TEST_F(InitialExportWorldTest, AbsentFightingHeroesReturnHeldEmptyListsInBothQueryModes)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	BattleInfo withoutHeroes(gameState().get());
	withoutHeroes.terrainType = battle()->getTerrainType();
	InitialExportWorld world(withoutHeroes);
	ASSERT_EQ(world.size(), 0);
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		for(const bool unstacked : {false, true})
		{
			const auto held = world.projectedHeroBonuses(side, unstacked);
			ASSERT_NE(held, nullptr);
			EXPECT_TRUE(held->empty());
		}
	EXPECT_THROW(world.projectedHeroBonuses(BattleSide::ALL_KNOWING, false), std::invalid_argument);
	EXPECT_EQ(world.size(), 0);
}

TEST_F(InitialExportWorldTest, HeroGraphQueryPreservesNativeStackedAndUnstackedSelection)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	auto world = std::make_unique<InitialExportWorld>(*battle());
	const auto origin = source(*world, *incoming);
	Bonus stronger = record(), weaker = record();
	stronger.type = weaker.type = BonusType::HERO_GRANTS_ATTACKS;
	stronger.subtype = weaker.subtype = BonusSubtypeID();
	stronger.limiter.reset();
	weaker.limiter.reset();
	weaker.val = 2;
	world->add(origin, stronger);
	world->add(origin, weaker);
	const CSelector witness = [](const Bonus * bonus) { return bonus->stacking == "initial-export-world.test"; };
	BonusList stacked, unstacked;
	world->projectedHeroBonuses(BattleSide::ATTACKER, false)->getBonuses(stacked, witness);
	world->projectedHeroBonuses(BattleSide::ATTACKER, true)->getBonuses(unstacked, witness);
	ASSERT_EQ(stacked.size(), 1);
	ASSERT_EQ(unstacked.size(), 2);
	EXPECT_EQ(stacked.totalValue(), 4);
	EXPECT_EQ(unstacked.totalValue(), 6);
	world.reset();
	incoming->addNewBonus(std::make_shared<Bonus>(stronger));
	incoming->addNewBonus(std::make_shared<Bonus>(weaker));
	const auto nativeStacked = attackerSideHero->getAllBonuses(witness);
	const auto nativeUnstacked = attackerSideHero->getUnstackedBonuses(witness);
	ASSERT_EQ(nativeStacked->size(), stacked.size());
	ASSERT_EQ(nativeUnstacked->size(), unstacked.size());
	EXPECT_EQ(projectedHitTest::serialized(*nativeStacked->front()), projectedHitTest::serialized(*stacked.front()));
	for(size_t index = 0; index < unstacked.size(); ++index)
		EXPECT_EQ(projectedHitTest::serialized(*(*nativeUnstacked)[index]), projectedHitTest::serialized(*unstacked[index]));
}

TEST_F(InitialExportWorldTest, QueryScalarCopyAcceptsFormChangesOfExactOriginalBodyAndKeepsOtherOwnersIsolated)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	const auto nativeBefore = incoming->save();
	// These are native-bound legacy preparation values, not arena-owned loans.
	// Finish their form/health queries before an immutable source graph is captured.
	auto state = incoming->acquireState();
	const auto original = state->battleFormOriginalCreature();
	const auto alternate = creatureByName("core:halberdier");
	ASSERT_NE(alternate, original);
	state->beginBattleForm(alternate, 2);
	int64_t damage = 7; state->damage(damage);
	const auto formed = state->save();
	ASSERT_NE(state->unitType(), incoming->unitType());
	auto restoredState = incoming->acquireState();
	*restoredState = *state; // Static CUnitState values only; preserve its native provider/environment.
	restoredState->endBattleForm();
	const auto restored = restoredState->save();
	InitialExportWorld world(*battle());
	const auto origin = source(world, *incoming);
	auto sibling = world.fork();
	const auto siblingBefore = sibling->snapshotAt(origin.occurrence);
	ASSERT_NO_THROW(world.copyQueryValues(origin, *state));
	world.recordQuerySnapshot(origin);
	EXPECT_EQ(world.snapshotAt(origin.occurrence), formed);
	EXPECT_EQ(world.unitAt(origin.occurrence).unitType()->getId(), alternate);
	ASSERT_NO_THROW(world.copyQueryValues(origin, *restoredState));
	world.recordQuerySnapshot(origin);
	EXPECT_EQ(world.snapshotAt(origin.occurrence), restored);
	EXPECT_EQ(world.unitAt(origin.occurrence).unitType()->getId(), original);
	EXPECT_EQ(sibling->snapshotAt(origin.occurrence), siblingBefore);
	EXPECT_EQ(incoming->save(), nativeBefore);
}

TEST_F(InitialExportWorldTest, QueryScalarCopyStillRejectsForeignOriginalSpeciesWithMatchingPacketIdentity)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld());
	ASSERT_NE(incoming->unitType(), braced->unitType());
	struct ForeignInfo final : battle::IUnitInfo
	{
		const CStack & physical;
		const CCreature * species;
		ForeignInfo(const CStack & physical, const CCreature * species) : physical(physical), species(species) {}
		int32_t unitBaseAmount() const override { return physical.unitBaseAmount(); }
		uint32_t unitId() const override { return physical.unitId(); }
		BattleSide unitSide() const override { return physical.unitSide(); }
		PlayerColor unitOwner() const override { return physical.unitOwner(); }
		SlotID unitSlot() const override { return physical.unitSlot(); }
		const CCreature * unitType() const override { return species; }
	} foreignInfo(*incoming, braced->unitType());
	const auto nativeBefore = incoming->save();
	InitialExportWorld world(*battle());
	const auto origin = source(world, *incoming);
	battle::CUnitStateDetached foreign(&foreignInfo, incoming);
	EXPECT_EQ(foreign.unitId(), incoming->unitId());
	EXPECT_EQ(foreign.unitSide(), incoming->unitSide());
	EXPECT_EQ(foreign.unitOwner(), incoming->unitOwner());
	EXPECT_THROW(world.copyQueryValues(origin, foreign), std::invalid_argument);
	EXPECT_EQ(incoming->save(), nativeBefore);
}
