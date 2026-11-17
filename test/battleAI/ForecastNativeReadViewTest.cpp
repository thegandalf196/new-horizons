/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapterFixture.h"
#include "../../AI/BattleAI/ForecastNativeReadView.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../AI/BattleAI/ProjectedBestAction.h"
#include "../../lib/battle/OwnedUnitCreatureResolver.h"
#include "../../lib/battle/UnitStateValueSnapshot.h"
#include "../../lib/battle/OwnedUnitStateLoanSource.h"
#include "../../lib/bonuses/BonusDependencyCapture.h"
#include "../../lib/bonuses/BonusDependencyQueryFrame.h"
#include "../../lib/IGameSettings.h"
#include <stdexcept>
namespace
{
struct OwnedNativeCase
{
	std::vector<forecast::NativeReadOccurrence> occurrences;
	std::shared_ptr<forecast::NativeReadEnvironment> environment;
	std::vector<std::shared_ptr<const forecast::NativeReadView>> views;
};
OwnedNativeCase captureOwned(HypotheticBattle & source)
{
	OwnedNativeCase result;
	const auto roster = source.battleGetAllUnits(false);
	std::map<CreatureID, battle::OwnedUnitCreatureResolver::Creature> prototypes;
	for(const auto * unit : roster)
	{
		const auto * type = unit->unitType();
		prototypes.try_emplace(type->getId(), battle::OwnedUnitCreatureResolver::Creature{
			type->getId(), type->getFactionID(), type->getLevel(), type->isDoubleWide(), type->getIconIndex(),
			type->warMachine, type->getFullRecruitCost(), type->getJsonKey(), type->getNamePluralTextID(),
			type->getNameSingularTextID(), type->getBaseHitPoints(), type->getAIValue()});
	}
	std::vector<battle::OwnedUnitCreatureResolver::Creature> descriptors;
	for(auto & [id, value] : prototypes) descriptors.push_back(std::move(value));
	auto creatures = std::make_shared<const battle::OwnedUnitCreatureResolver>(std::move(descriptors));
	result.environment = std::make_shared<forecast::NativeReadEnvironment>();
	auto & env = *result.environment;
	const auto sides = battle::ProjectedUnitEnvironmentState::capture(*source.getBattle());
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		env.inputs.at(side) = sides.inputs(side);
		env.states.at(side) = sides.state(side);
	}
	env.magic = source.getCapturedMagicRules();
	env.skeletonUpgrades = CreatureID(CreatureID::SKELETON).toCreature()->upgrades;
	const auto skill = SecondarySkill::decode("new-horizons:necromancy");
	if(skill >= 0) env.necromancyLuckSource = SecondarySkill(skill);
	const auto & settings = *LIBRARY->engineSettings();
	env.legacyMoraleLimits = std::pair{-static_cast<int32_t>(settings.getVector(EGameSettings::COMBAT_BAD_MORALE_CHANCE).size()),
		static_cast<int32_t>(settings.getVector(EGameSettings::COMBAT_GOOD_MORALE_CHANCE).size())};
	env.legacyLuckLimits = std::pair{-static_cast<int32_t>(settings.getVector(EGameSettings::COMBAT_BAD_LUCK_CHANCE).size()),
		static_cast<int32_t>(settings.getVector(EGameSettings::COMBAT_GOOD_LUCK_CHANCE).size())};
	for(const auto * observed : roster)
	{
		const auto unit = source.getForUpdate(observed->unitId());
		auto values = std::make_shared<const battle::UnitStateValueSnapshot>(battle::UnitStateValueSnapshot::capture(*unit));
		auto descriptor = std::make_shared<const forecast::NativeReadDescriptor>(forecast::NativeReadDescriptor{
			values->identity(), {unit->getBattleStartMaximumAggregateHP(), unit->getRebirthOriginalAggregateHP()}, creatures});
		bonusDependencies::QueryFrameBinding binding{unit->unitId(), unit->creatureId(), unit->getCount(),
			unit->unitOwner(), unit->unitSide(), unit->unitSlot(), unit->ghost, unit->summoned, unit->natureSummoned, creatures};
		auto program = unit->captureModifierFacts(nullptr);
		if(!program) throw std::logic_error("Native test source has no closed C5 raw program");
		program = program->bindQueries(binding, unit->localSpellEffects());
		if(!program) throw std::logic_error("Native test source cannot bind C5 queries");
		auto frame = program->refreshQueries(binding, bonusDependencies::Change::None);
		if(!frame) throw std::logic_error("Native test source cannot materialize C5 queries");
		result.occurrences.push_back({std::move(descriptor), std::move(values), std::move(program), std::move(frame)});
		env.warfare[unit->unitId()] = source.unitWarfareStatBonus(unit.get());
	}
	result.views = forecast::NativeReadView::fromOwned(result.occurrences, result.environment);
	return result;
}
struct NativeObservation
{
	JsonNode state;
	std::array<int64_t, 38> values;
	bool operator==(const NativeObservation &) const = default;
};
NativeObservation observe(const battle::CUnitState & unit)
{
	// Values come from the REAL native APIs, never reimplemented arithmetic.
	const std::array<int64_t, 38> values{unit.getCount(), unit.getAvailableHealth(), unit.getFirstHPleft(),
		unit.alive(), unit.isGhost(), unit.moved(), unit.waited(), unit.defended(), unit.canMove(),
		unit.shots.available(), unit.shots.total(), unit.shots.isLimited(),
		unit.counterAttacks.available(), unit.counterAttacks.total(), unit.counterAttacks.isLimited(),
		unit.casts.available(), unit.getAttack(false), unit.getDefense(false), unit.getMinDamage(false),
		unit.getMaxDamage(false), unit.getMovementRange(), unit.getInitiative(), unit.getMaxHealth(), unit.battleQueuePhase(0),
		unit.getAttack(true), unit.getDefense(true), unit.getMinDamage(true), unit.getMaxDamage(true),
		unit.getTotalAttacks(false), unit.getTotalAttacks(true), unit.isShooter(), unit.canShoot(), unit.canCast(),
		unit.magicResistance(), unit.moraleVal(), unit.luckVal(),
		unit.getBattleStartMaximumAggregateHP(), unit.getRebirthOriginalAggregateHP()};
	// Native retaliation totals have a monotonic cache: observe it BEFORE the
	// native save, so repeated observations compare the same frozen state.
	return {unit.acquireState()->save(), values};
}
}
TEST_F(ForecastNativeAdapterTest, CompleteSnapshotRestoresCanonicalValuesWithoutReplacingReceiverOwners)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:archer"));
	const auto id = parent->battleGetAllUnits(false).front()->unitId();
	auto source = parent->getForUpdate(id);
	source->defending = true; source->waiting = true; source->waitedThisTurn = true;
	source->archeryCrossfireAttackers = {21, 22}; source->forestAmbush.attacked = true;
	source->shots.use(); source->counterAttacks.use();
	int64_t damage = 17;
	source->damage(damage, false, battle::DamageProvenance::SPELL);
	source->health.setTemporaryHitPoints(13);
	source->guardianSpiritHitPoints = 17; source->guardianSpiritRoundsRemaining = 2;
	source->demonicGateOpened = true; source->cleaveUsedThisActivation = true;
	const auto snapshot = battle::UnitStateValueSnapshot::capture(*source);
	const auto expected = observe(*source);
	auto receiver = source->acquireState();
	const auto beforeContext = receiver->health.valueContext();
	receiver->afterNewRound(false); receiver->archeryCrossfireAttackers.clear();
	snapshot.restore(*receiver);
	EXPECT_EQ(observe(*receiver), expected);
	EXPECT_EQ(receiver->health.valueContext(), beforeContext);
	EXPECT_FALSE(receiver->health.isDetached()); // Receiver remains native Unit-owned.
	const auto sourceHp = source->getAvailableHealth();
	damage = 1; receiver->damage(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(source->getAvailableHealth(), sourceHp);
	EXPECT_EQ(source->guardianSpiritHitPoints, 17);
	EXPECT_EQ(receiver->guardianSpiritHitPoints, 16);
	const auto wrong = parent->getForUpdate(parent->battleGetAllUnits(false).back()->unitId());
	const auto wrongBefore = wrong->save();
	EXPECT_THROW(snapshot.restore(*wrong), std::invalid_argument);
	EXPECT_EQ(wrong->save(), wrongBefore);
}
TEST_F(ForecastNativeAdapterTest, OwnedNativeReadersAndNestedAcquireRemainStableAfterAncestorChangesAndDestruction)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:archer", true));
	const auto id = parent->battleGetAllUnits(false).front()->unitId();
	auto source = parent->getForUpdate(id);
	source->waiting = true; source->waitedThisTurn = true; source->defending = true;
	source->addUnitBonus(newHorizonsFocusMagic::enchantment(*source,
		SpellID(SpellID::decode(newHorizonsFocusMagic::SPELL))));
	const auto expected = observe(*source);
	const auto rng = projectedHitTest::serialized(*handler->randomizer);
	const auto ambient = projectedHitTest::serialized(CRandomGenerator::getDefault());
	auto owned = captureOwned(*parent);
	EXPECT_EQ(owned.views.front()->work().materialized, 0u);
	auto child = owned.views.front();
	auto sibling = child; // Unchanged fork is a handle copy, no native clone.
	auto retained = child->retainUnit();
	EXPECT_EQ(observe(child->unit()), expected);
#ifdef NH_PERF_EXPERIMENTS
	const auto beforeLoans = child->work().owningLoans;
#endif
	auto loan = retained->acquireState(); // ACTUAL Unit API must retain its group.
	auto nested = loan->acquireState(); // No captured parent-loan environment.
#ifdef NH_PERF_EXPERIMENTS
	EXPECT_GT(child->work().owningLoans, beforeLoans);
#endif
	int64_t damage = 37;
	source->damage(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	source->waiting = false; source->defending = false; source->counterAttacks.use();
	source->removeUnitBonus(newHorizonsFocusMagic::nativeRecords(*source));
	EXPECT_EQ(observe(child->unit()), expected);
	EXPECT_EQ(observe(sibling->unit()), expected);
	EXPECT_EQ(observe(*loan), expected); EXPECT_EQ(observe(*nested), expected);
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), rng);
	const std::weak_ptr<CGameState> world = gameState();
	source.reset(); parent.reset(); visible.reset(); handler.reset(); server.gameState.reset();
	environment.reset();
	TinyMapGameTest::TearDown(); // Idempotent native fixture teardown: actually destroy the source world.
	EXPECT_TRUE(world.expired());
	owned.views.clear(); owned.occurrences.clear(); child.reset(); sibling.reset();
	EXPECT_EQ(observe(*loan), expected); EXPECT_EQ(observe(*nested), expected);
	retained.reset(); loan.reset(); // The nested loan must not borrow either predecessor.
	EXPECT_EQ(observe(*nested), expected);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), ambient);
}
TEST_F(ForecastNativeAdapterTest, OwnScratchCountChangesRefreshRawQueriesAndPreserveNativeDamageParity)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto source = parent->getForUpdate(parent->battleGetAllUnits(false).front()->unitId());
	auto owned = captureOwned(*parent);
	auto actual = owned.views.front()->unit().acquireState();
	auto expected = source->acquireState();
	const auto original = observe(owned.views.front()->unit());
	int64_t damage = source->getMaxHealth() + 1, nativeDamage = damage;
	expected->damage(nativeDamage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	actual->damage(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(damage, nativeDamage); EXPECT_EQ(observe(*actual), observe(*expected));
	EXPECT_EQ(observe(owned.views.front()->unit()), original);
#ifdef NH_PERF_EXPERIMENTS
	EXPECT_GT(owned.views.front()->work().queryRebindings, 0u);
#endif
	int64_t healed = 17, nativeHealed = healed;
	const auto a = actual->heal(healed, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE);
	const auto b = expected->heal(nativeHealed, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE);
	EXPECT_EQ(a.healedHealthPoints, b.healedHealthPoints);
	EXPECT_EQ(a.resurrectedCount, b.resurrectedCount);
	EXPECT_EQ(observe(*actual), observe(*expected));
	auto nested = actual->acquireState();
	EXPECT_EQ(observe(*nested), observe(*actual));
}
TEST_F(ForecastNativeAdapterTest, WrongBindingsUnknownEnvironmentAndForeignSameIdCannotBorrowNativeState)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto owned = captureOwned(*parent);
	auto valid = owned.views.front();
	const auto before = observe(valid->unit());
	auto bad = owned.occurrences;
	bad.front().values = bad.back().values;
	EXPECT_THROW(forecast::NativeReadView::fromOwned(bad, owned.environment), std::invalid_argument);
	auto unknown = std::make_shared<forecast::NativeReadEnvironment>(*owned.environment);
	unknown->states.at(BattleSide::ATTACKER).reset();
	EXPECT_THROW(forecast::NativeReadView::fromOwned(owned.occurrences, unknown), std::invalid_argument);
	EXPECT_EQ(observe(valid->unit()), before);
	// Equal numeric IDs/count/body are insufficient: a separate descriptor
	// capability must not be substituted for this program/frame's bound owner.
	const auto distinct = captureOwned(*parent);
	bad = owned.occurrences;
	bad.front().descriptor = distinct.occurrences.front().descriptor;
	ASSERT_EQ(bad.front().descriptor->identity, owned.occurrences.front().descriptor->identity);
	EXPECT_THROW(forecast::NativeReadView::fromOwned(bad, owned.environment), std::invalid_argument);
	EXPECT_EQ(observe(valid->unit()), before);
}

TEST_F(ForecastNativeAdapterTest, ActualForkNativeLookupsDoNotReadAnAdvancingAncestor)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:archer"));
	const auto attackers = parent->battleGetUnitsIf([](const battle::Unit * unit)
	{ return unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0); });
	ASSERT_EQ(attackers.size(), 1u);
	ASSERT_EQ(attackers.front()->creatureId(), CreatureID(CreatureID::decode("core:archer")));
	ASSERT_TRUE(attackers.front()->isShooter()); // Intrinsic shots must survive Focus expiry.
	const auto id = attackers.front()->unitId();
	auto source = parent->getForUpdate(id);
	source->waiting = true; source->waitedThisTurn = true; source->defending = true;
	source->addUnitBonus(newHorizonsFocusMagic::enchantment(*source,
		SpellID(SpellID::decode(newHorizonsFocusMagic::SPELL))));
	const auto expected = observe(*source);
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent,
		std::shared_ptr<const battle::OwnedBattleFrameBatch>{}, parent->searchBudget());
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent,
		std::shared_ptr<const battle::OwnedBattleFrameBatch>{}, parent->searchBudget());
	forecast::NativeAdapter::forkExchange(*parent, *child);
	forecast::NativeAdapter::forkExchange(*parent, *sibling);
	const auto * childUnit = dynamic_cast<const battle::CUnitState *>(child->battleGetUnitByID(id));
	ASSERT_NE(childUnit, nullptr);
	ASSERT_NE(childUnit, source.get());
	EXPECT_EQ(observe(*childUnit), expected);
	auto expectedChild = childUnit->acquireState();
	const auto retainedOriginal = child->forecastState()->unit(id)->readView;
	ASSERT_TRUE(retainedOriginal);
	const auto childMap = child->stackStates;
	const auto siblingMap = sibling->stackStates;
	ASSERT_TRUE(child->beginLifeDrainFriendlyObservation(id));
	const auto authority = projectedHitTest::serialized(*handler->randomizer);
	const auto ambient = projectedHitTest::serialized(CRandomGenerator::getDefault());
	int64_t damage = source->getMaxHealth() + 1;
	ASSERT_TRUE(forecast::NativeAdapter::damage(*parent, *source, damage,
		battle::DamageProvenance::PHYSICAL_CREATURE, nullptr));
	focusMagicAI::afterAttack(*source, true, false, true, true, parent.get());
	EXPECT_EQ(observe(*childUnit), expected);
	const auto * other = dynamic_cast<const battle::CUnitState *>(sibling->battleGetUnitByID(id));
	ASSERT_NE(other, nullptr); EXPECT_EQ(observe(*other), expected);
	EXPECT_EQ(child->stackStates, childMap); EXPECT_EQ(sibling->stackStates, siblingMap);
	const auto selected = child->battleGetUnitsIf([=](const battle::Unit * unit) { return unit->unitId() == id; });
	ASSERT_EQ(selected.size(), 1u); EXPECT_EQ(selected.front(), childUnit);
	EXPECT_TRUE(child->finishLifeDrainFriendlyObservation());
	auto writable = child->getForUpdate(id); // Must initialize from OWNED old snapshot, not changed source.
	EXPECT_EQ(observe(*writable), expected);
	damage = writable->getMaxHealth() + 1;
	int64_t expectedDamage = damage;
	expectedChild->damage(expectedDamage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	ASSERT_TRUE(forecast::NativeAdapter::damage(*child, *writable, damage,
		battle::DamageProvenance::PHYSICAL_CREATURE, nullptr));
	EXPECT_EQ(observe(*writable), observe(*expectedChild));
	EXPECT_EQ(observe(retainedOriginal->unit()), expected);
	EXPECT_EQ(observe(*other), expected);
	const auto hpBeforeExpiry = writable->getAvailableHealth();
	const auto shotsBeforeExpiry = writable->shots.available();
	ASSERT_TRUE(forecast::NativeAdapter::expireEffectSelector(*child, id,
		Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::decode(newHorizonsFocusMagic::SPELL))))));
	EXPECT_EQ(newHorizonsFocusMagic::charges(*writable), 0);
	EXPECT_EQ(writable->getAvailableHealth(), hpBeforeExpiry);
	EXPECT_EQ(writable->shots.available(), shotsBeforeExpiry);
	EXPECT_EQ(newHorizonsFocusMagic::charges(retainedOriginal->unit()), 3);
	DamageCache cache;
	PotentialTargets candidates(writable.get(), cache, child);
	ASSERT_FALSE(candidates.possibleAttacks.empty());
	const auto accepted = candidates.bestAction();
	applyProjectedBestAction(*child, writable.get(), accepted);
	EXPECT_EQ(writable->save(), accepted.attackerState->save());
	EXPECT_EQ(child->forecastState()->unit(id)->health.available(), writable->getAvailableHealth());
	EXPECT_EQ(observe(*other), expected);
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), authority);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), ambient);
}
TEST_F(ForecastNativeAdapterTest, FrozenOrderedCartContextSurvivesAncestorDeathAndUpdatesOnlyWithNewGroup)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:archer", true));
	uint32_t actorId = 0, cartId = 0;
	for(const auto * unit : parent->battleGetAllUnits(false))
		if(unit->isAmmoCart()) cartId = unit->unitId();
		else if(unit->unitSide() == BattleSide::ATTACKER) actorId = unit->unitId();
	auto owned = captureOwned(*parent);
	const forecast::NativeReadView * reader = nullptr;
	for(const auto & view : owned.views) if(view->valuesOwner()->identity().id == actorId) reader = view.get();
	ASSERT_NE(reader, nullptr);
	EXPECT_FALSE(reader->unit().shots.isLimited());
	auto cart = parent->getForUpdate(cartId);
	int64_t lethal = cart->getAvailableHealth();
	cart->damage(lethal, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	ASSERT_FALSE(cart->alive());
	EXPECT_FALSE(reader->unit().shots.isLimited());
	auto current = captureOwned(*parent);
	for(const auto & view : current.views)
		if(view->valuesOwner()->identity().id == actorId)
			EXPECT_EQ(view->unit().shots.isLimited(), parent->getForUpdate(actorId)->shots.isLimited());
}

TEST_F(ForecastNativeAdapterTest, DirectOwnedAcquisitionAvoidsIntermediateReadCloneAndHasNoGroupCycle)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto owned = captureOwned(*parent);
	const auto id = owned.occurrences.front().descriptor->identity.id;
	const auto expected = observe(*parent->getForUpdate(id));
	auto acquisition = forecast::NativeReadView::acquireFromOwned(owned.occurrences, owned.environment, id);
	ASSERT_TRUE(acquisition.unit && acquisition.owner);
	EXPECT_EQ(acquisition.owner->work().materialized, 0u);
#ifdef NH_PERF_EXPERIMENTS
	EXPECT_EQ(acquisition.owner->work().owningLoans, 1u);
#endif
	const std::weak_ptr<const void> group = acquisition.owner->workIdentity();
	EXPECT_EQ(observe(*acquisition.unit), expected);
	acquisition.owner.reset();
	EXPECT_FALSE(group.expired());
	EXPECT_EQ(observe(*acquisition.unit), expected);
	acquisition.unit.reset();
	EXPECT_TRUE(group.expired());
}
TEST_F(ForecastNativeAdapterTest, ExplicitLoanCapabilityNeverFallsBackWhenAnOwningProviderReturnsEmpty)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	class Conditional final : public battle::IUnitEnvironment, public battle::OwnedUnitStateLoanSource
	{
	public:
		bool enabled = false;
		const battle::OwnedUnitStateLoanSource * ownedUnitStateLoanSource() const noexcept override
		{
			return enabled ? this : nullptr;
		}
		bool unitHasAmmoCart(const battle::Unit *) const override { return false; }
		PlayerColor unitEffectiveOwner(const battle::Unit * unit) const override { return unit->unitOwner(); }
		std::shared_ptr<battle::CUnitState> acquireOwnedState(const battle::CUnitState &) const override { return {}; }
	} context;
	auto state = parent->getForUpdate(parent->battleGetAllUnits(false).front()->unitId())->acquireState();
	state->bindStateEnvironment(&context);
	const auto before = state->save();
	EXPECT_TRUE(state->acquire()); EXPECT_TRUE(state->acquireState()); // Explicit native/reference path.
	context.enabled = true;
	EXPECT_THROW(state->acquire(), std::logic_error);
	EXPECT_THROW(state->acquireState(), std::logic_error);
	EXPECT_EQ(state->save(), before);
}

TEST_F(ForecastNativeAdapterTest, PublicBuilderAliasesCannotAdvanceAlreadyFrozenReadGroups)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto owned = captureOwned(*parent);
	auto inputs = owned.occurrences;
	auto mutableDescriptor = std::make_shared<forecast::NativeReadDescriptor>(*inputs.front().descriptor);
	inputs.front().descriptor = mutableDescriptor;
	auto mutableEnvironment = std::make_shared<forecast::NativeReadEnvironment>(*owned.environment);
	auto views = forecast::NativeReadView::fromOwned(inputs, mutableEnvironment);
	const auto before = observe(views.front()->unit());
	mutableDescriptor->identity.id += 1000;
	mutableEnvironment->states.at(BattleSide::ATTACKER)->bloodrageDamagePercent += 100;
	mutableEnvironment->warfare.begin()->second += 100;
	EXPECT_EQ(observe(views.front()->unit()), before);
	EXPECT_NE(views.front()->environmentOwner().get(), mutableEnvironment.get());
	EXPECT_EQ(views.front()->workIdentity(), views.back()->workIdentity());
}

TEST_F(ForecastNativeAdapterTest, OwnedQuerySourceProvesExactReceiverAndCountDerivedLoanOnly)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto owned = captureOwned(*parent);
	const auto view = owned.views.front();
	const auto & reader = view->unit();
	auto proof = forecast::NativeReadView::querySource(reader);
	ASSERT_TRUE(proof);
	EXPECT_EQ(proof->modifiers, owned.occurrences.front().modifiers);
	EXPECT_TRUE(proof->modifiers->ownsQueryFrame(*proof->queries));
	EXPECT_EQ(proof->descriptor->creatures, owned.occurrences.front().descriptor->creatures);
	EXPECT_FALSE(forecast::NativeReadView::querySource(*parent->getForUpdate(reader.unitId())));
	// Borrowing the exact info/provider still does not own the receiver itself.
	battle::CUnitStateDetached borrowed(&reader, reader.getBonusBearer());
	borrowed = reader;
	EXPECT_FALSE(forecast::NativeReadView::querySource(borrowed));
	auto loan = reader.acquireState();
	int64_t damage = loan->getMaxHealth();
	loan->damage(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	auto derived = forecast::NativeReadView::querySource(*loan);
	ASSERT_TRUE(derived);
	EXPECT_EQ(derived->modifiers, proof->modifiers);
	EXPECT_EQ(derived->queries->binding().count, loan->getCount());
	EXPECT_NE(derived->queries->binding().count, proof->queries->binding().count);
	EXPECT_EQ(forecast::NativeReadView::querySource(reader)->queries, proof->queries);
	auto retainedProgram = derived->modifiers;
	owned.views.clear();
	EXPECT_TRUE(retainedProgram->ownsQueryFrame(*derived->queries));
}

TEST_F(ForecastNativeAdapterTest, ForkedReadonlyRetaliationQueriesDoNotLeakAcrossSiblingExpiry)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto id = parent->battleGetAllUnits(false).front()->unitId();
	auto source = parent->getForUpdate(id); source->counterAttacks.reset();
	Bonus extra; extra.type = BonusType::ADDITIONAL_RETALIATION; extra.val = 1;
	extra.source = BonusSource::SPELL_EFFECT; extra.sid = BonusSourceID(SpellID(SpellID::HASTE));
	extra.duration = BonusDuration::N_TURNS; extra.turnsRemain = 1;
	source->addUnitBonus({extra});
	auto nativeObserved = source->acquireState(), nativeUnobserved = source->acquireState();
	auto owned = captureOwned(*parent);
	const auto raw = battle::UnitStateValueSnapshot::retaliationCache(*source);
	auto left = forecast::NativeReadView::forkViews(owned.views);
	auto right = forecast::NativeReadView::forkViews(owned.views);
	EXPECT_EQ(left.front()->work().materialized, 0u);
	EXPECT_EQ(right.front()->work().materialized, 0u);
	EXPECT_NE(left.front()->workIdentity(), right.front()->workIdentity());
	EXPECT_EQ(left.front()->unit().counterAttacks.total(), nativeObserved->counterAttacks.total());
	EXPECT_EQ(right.front()->valuesOwner()->retaliationCache(),
		battle::UnitStateValueSnapshot::retaliationCache(*nativeUnobserved));
	EXPECT_EQ(battle::UnitStateValueSnapshot::retaliationCache(*source), raw);
	// Native expiration before the unobserved sibling's FIRST total() call.
	source->removeUnitBonus(Selector::type()(BonusType::ADDITIONAL_RETALIATION));
	auto expired = captureOwned(*parent);
	const auto rebind = [&](const auto & branch)
	{
		auto occurrences = expired.occurrences;
		for(size_t index = 0; index < occurrences.size(); ++index)
			occurrences[index].values = std::make_shared<const battle::UnitStateValueSnapshot>(
				occurrences[index].values->withRetaliationCache(branch[index]->valuesOwner()->retaliationCache()));
		return forecast::NativeReadView::fromOwned(std::move(occurrences), expired.environment);
	};
	auto expiredLeft = rebind(left), expiredRight = rebind(right);
	EXPECT_EQ(expiredLeft.front()->unit().counterAttacks.total(), nativeObserved->counterAttacks.total());
	EXPECT_EQ(expiredRight.front()->unit().counterAttacks.total(), nativeUnobserved->counterAttacks.total());
	EXPECT_GT(nativeObserved->counterAttacks.total(), nativeUnobserved->counterAttacks.total());
	EXPECT_EQ(battle::UnitStateValueSnapshot::retaliationCache(*source), raw);
}
TEST_F(ForecastNativeAdapterTest, RetaliationForkInheritsOnlyActualParentObservationsAndOutlivesParent)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto owned = captureOwned(*parent);
	const auto rng = projectedHitTest::serialized(*handler->randomizer);
	const auto ambient = projectedHitTest::serialized(CRandomGenerator::getDefault());
	const auto inherited = owned.views.front()->unit().counterAttacks.total();
	const auto raw = owned.views.front()->valuesOwner()->retaliationCache();
	auto fork = forecast::NativeReadView::forkViews(owned.views);
	EXPECT_EQ(fork.front()->valuesOwner()->retaliationCache(), raw);
	EXPECT_EQ(fork.front()->environmentOwner(), owned.views.front()->environmentOwner());
	EXPECT_EQ(fork.front()->queryFrame(), owned.views.front()->queryFrame());
	EXPECT_EQ(fork.front()->work().owningLoans, 0u);
	owned.views.clear(); owned.occurrences.clear(); owned.environment.reset();
	EXPECT_EQ(fork.front()->unit().counterAttacks.total(), inherited);
	auto loan = fork.front()->unit().acquireState();
	EXPECT_EQ(battle::UnitStateValueSnapshot::retaliationCache(*loan), raw);
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), rng);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), ambient);
}
TEST_F(ForecastNativeAdapterTest, CounterFactObservationAndRejectedForkDoNotLatchSourceHistory)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto id = parent->battleGetAllUnits(false).front()->unitId();
	auto source = parent->getForUpdate(id); source->counterAttacks.reset();
	const auto raw = battle::UnitStateValueSnapshot::retaliationCache(*source);
	auto native = source->acquireState();
	const auto facts = source->counterAttacks.observeWithoutLatch();
	EXPECT_EQ(facts.total, native->counterAttacks.total());
	EXPECT_EQ(facts.available, native->counterAttacks.available());
	EXPECT_EQ(facts.limited, native->counterAttacks.isLimited());
	EXPECT_EQ(battle::UnitStateValueSnapshot::retaliationCache(*source), raw);
	auto owned = captureOwned(*parent);
	const auto originalWork = owned.views.front()->workIdentity();
	auto invalid = owned.views; invalid.push_back(nullptr);
	EXPECT_THROW(forecast::NativeReadView::forkViews(invalid), std::invalid_argument);
	EXPECT_EQ(owned.views.front()->workIdentity(), originalWork);
	EXPECT_EQ(battle::UnitStateValueSnapshot::retaliationCache(*source), raw);
	EXPECT_EQ(owned.views.front()->work().materialized, 0u);
}
