/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapterFixture.h"
namespace
{
std::optional<forecastEffects::RecordId> managedFocusMarker(const forecast::State & state, uint32_t id)
{
	for(const auto & record : state.unit(id)->effects->values())
		if(record.kind == forecastEffects::Kind::FocusManaged && record.value->type == BonusType::NONE)
			return record.id;
	return {};
}
}
TEST_F(ForecastNativeAdapterTest, FocusAcceptedZeroDamageUsesThreeChargesAndKeepsStableTargetAndParents)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto attackers = parent->battleGetUnitsIf([](const battle::Unit * unit)
	{ return unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0); });
	ASSERT_EQ(attackers.size(), 1u);
	ASSERT_EQ(attackers.front()->creatureId(), CreatureID(CreatureID::decode("core:pikeman")));
	const auto id = attackers.front()->unitId();
	auto root = parent->getForUpdate(id);
	ASSERT_FALSE(root->isShooter());
	root->addUnitBonus(newHorizonsFocusMagic::enchantment(*root,
		SpellID(SpellID::decode(newHorizonsFocusMagic::SPELL))));
	const auto rng = projectedHitTest::serialized(*handler->randomizer);
	const auto ambient = projectedHitTest::serialized(CRandomGenerator::getDefault());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *child);
	forecast::NativeAdapter::forkExchange(*parent, *sibling);
	auto actor = child->getForUpdate(id);
	const auto * stable = actor.get();
	const auto hp = actor->getAvailableHealth();
	auto marker = managedFocusMarker(*child->forecastState(), id);
	ASSERT_TRUE(marker);
	for(int remaining = 2; remaining >= 0; --remaining)
	{
		focusMagicAI::afterAttack(*actor, true, false, true, true, child.get());
		EXPECT_EQ(child->battleGetUnitByID(id), stable);
		EXPECT_EQ(newHorizonsFocusMagic::charges(*actor), remaining);
		EXPECT_EQ(child->forecastState()->unit(id)->focus->charges, remaining);
		EXPECT_EQ(child->forecastState()->unit(id)->activation.shots.available, actor->shots.available());
		EXPECT_EQ(actor->getAvailableHealth(), hp); // Real zero-damage/miss aftermath.
		const auto replacement = managedFocusMarker(*child->forecastState(), id);
		if(remaining > 0) { ASSERT_TRUE(replacement); EXPECT_GT(*replacement, *marker); }
		else EXPECT_FALSE(replacement);
		marker = replacement;
	}
	EXPECT_FALSE(actor->isShooter());
	EXPECT_EQ(parent->forecastState()->unit(id)->focus->charges, 3);
	EXPECT_EQ(sibling->forecastState()->unit(id)->focus->charges, 3);
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), rng);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), ambient);
}

TEST_F(ForecastNativeAdapterTest, FocusNativeArcherFinalChargeThenCasualtyRefreshMatchesReference)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:archer"));
	// Roster order follows native initiative ordering, not attacker/defender roles.
	const auto attackers = parent->battleGetUnitsIf([](const battle::Unit * unit)
	{ return unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0); });
	const auto defenders = parent->battleGetUnitsIf([](const battle::Unit * unit)
	{ return unit->unitSide() == BattleSide::DEFENDER && unit->unitSlot() == SlotID(0); });
	ASSERT_EQ(attackers.size(), 1u); ASSERT_EQ(defenders.size(), 1u);
	ASSERT_EQ(attackers.front()->creatureId(), CreatureID(CreatureID::decode("core:archer")));
	ASSERT_EQ(defenders.front()->creatureId(), CreatureID(CreatureID::decode("core:pikeman")));
	const auto id = attackers.front()->unitId();
	const auto targetId = defenders.front()->unitId();
	auto root = parent->getForUpdate(id);
	ASSERT_TRUE(root->isShooter());
	root->addUnitBonus(newHorizonsFocusMagic::enchantment(*root,
		SpellID(SpellID::decode(newHorizonsFocusMagic::SPELL))));
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *child);
	auto actor = child->getForUpdate(id), expected = reference->getForUpdate(id);
	const auto ammo = actor->shots.available();
	for(int index = 0; index < 3; ++index)
	{
		focusMagicAI::afterAttack(*actor, true, false, true, true, child.get());
		focusMagicAI::afterAttack(*expected, true, false, true);
		EXPECT_EQ(actor->shots.available(), ammo - index - 1);
	}
	ASSERT_EQ(newHorizonsFocusMagic::charges(*actor), 0);
	int64_t damage = actor->getMaxHealth() + 1, nativeDamage = damage;
	expected->damage(nativeDamage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	ASSERT_TRUE(forecast::NativeAdapter::damage(*child, *actor, damage, battle::DamageProvenance::PHYSICAL_CREATURE, nullptr));
	const auto actual = child->calculateDmgRange(BattleAttackInfo(actor.get(), child->getForUpdate(targetId).get(), 0, true));
	const auto baseline = reference->calculateDmgRange(BattleAttackInfo(expected.get(), reference->getForUpdate(targetId).get(), 0, true));
	EXPECT_EQ(actual.damage.min, baseline.damage.min);
	EXPECT_EQ(actual.damage.max, baseline.damage.max); // Removed +20% must not reappear from a stale modifier recipe.
}

TEST_F(ForecastNativeAdapterTest, FocusRealAmmoCartKeepsNativeAmmoUnlimitedUntilCartDies)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:archer", true));
	std::shared_ptr<StackWithBonuses> root, cart;
	for(const auto * unit : parent->battleGetAllUnits(false))
		if(unit->unitSide() == BattleSide::ATTACKER)
		{
			if(unit->unitSlot() == SlotID(1) && unit->isAmmoCart()) cart = parent->getForUpdate(unit->unitId());
			else if(unit->unitSlot() == SlotID(0)) root = parent->getForUpdate(unit->unitId());
		}
	ASSERT_TRUE(root); ASSERT_TRUE(cart);
	ASSERT_EQ(root->creatureId(), CreatureID(CreatureID::decode("core:archer")));
	ASSERT_EQ(cart->creatureId(), CreatureID::AMMO_CART);
	ASSERT_TRUE(root->isShooter());
	root->addUnitBonus(newHorizonsFocusMagic::enchantment(*root,
		SpellID(SpellID::decode(newHorizonsFocusMagic::SPELL))));
	ASSERT_FALSE(root->shots.isLimited());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent)); // Must not silently test the fallback path.
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *child);
	auto actor = child->getForUpdate(root->unitId());
	const auto ordinary = actor->shots.available();
	for(int index = 0; index < 3; ++index)
	{
		focusMagicAI::afterAttack(*actor, true, false, true, true, child.get());
		EXPECT_EQ(actor->shots.available(), ordinary);
		EXPECT_FALSE(actor->shots.isLimited());
	}
	auto victim = child->getForUpdate(cart->unitId());
	int64_t killed = victim->getAvailableHealth();
	ASSERT_TRUE(forecast::NativeAdapter::damage(*child, *victim, killed, battle::DamageProvenance::PHYSICAL_CREATURE, nullptr));
	ASSERT_FALSE(victim->alive());
	ASSERT_TRUE(actor->shots.isLimited());
	focusMagicAI::afterAttack(*actor, true, false, true, true, child.get());
	EXPECT_EQ(actor->shots.available(), ordinary - 1);
	EXPECT_EQ(child->forecastState()->unit(actor->unitId())->activation.shots.available, ordinary - 1);
}

TEST_F(ForecastNativeAdapterTest, FocusSelectedReceiptImportsNativeTwoChargesWithoutSecondConsumption)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto attackers = parent->battleGetUnitsIf([](const battle::Unit * unit)
	{ return unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0); });
	ASSERT_EQ(attackers.size(), 1u);
	ASSERT_EQ(attackers.front()->creatureId(), CreatureID(CreatureID::decode("core:pikeman")));
	const auto id = attackers.front()->unitId();
	auto root = parent->getForUpdate(id);
	ASSERT_FALSE(root->isShooter());
	root->addUnitBonus(newHorizonsFocusMagic::enchantment(*root,
		SpellID(SpellID::decode(newHorizonsFocusMagic::SPELL))));
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto acceptedWorld = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto destination = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *acceptedWorld);
	forecast::NativeAdapter::forkExchange(*parent, *destination);
	auto accepted = acceptedWorld->getForUpdate(id), target = destination->getForUpdate(id);
	const auto defenders = acceptedWorld->battleGetUnitsIf([](const battle::Unit * unit)
	{ return unit->unitSide() == BattleSide::DEFENDER && unit->unitSlot() == SlotID(0); });
	ASSERT_EQ(defenders.size(), 1u);
	ASSERT_EQ(defenders.front()->creatureId(), CreatureID(CreatureID::decode("core:pikeman")));
	const auto opponent = defenders.front()->unitId();
	DamageCache cache;
	cache.buildDamageCache(acceptedWorld, BattleSide::ATTACKER);
	const auto query = std::make_shared<const VisibleCandidateWorld>(acceptedWorld);
	const auto receipt = VisibleCandidateWorld::evaluate(query, accepted.get(),
		acceptedWorld->battleGetUnitByID(opponent), 0, true, accepted->getPosition(), cache);
	ASSERT_TRUE(receipt.snapshotSeal);
	ASSERT_NE(receipt.attackerState, nullptr);
	ASSERT_EQ(newHorizonsFocusMagic::charges(*receipt.attackerState), 2);
	ASSERT_EQ(newHorizonsFocusMagic::charges(*accepted), 3); // Preview leaves its source unchanged.
	selectedAttackReceipt::restoreResources(receipt, *receipt.attackerState, target, target, *destination);
	EXPECT_EQ(newHorizonsFocusMagic::charges(*target), 2);
	EXPECT_EQ(destination->forecastState()->unit(id)->focus->charges, 2);
	selectedAttackReceipt::restoreResources(receipt, *receipt.attackerState, target, target, *destination);
	EXPECT_EQ(newHorizonsFocusMagic::charges(*target), 2); // Reopening/import is idempotent, never3→2→1.
	const auto beforeForeign = target->save();
	acceptedWorld->nextRound();
	EXPECT_THROW(selectedAttackReceipt::restoreResources(receipt, *receipt.attackerState, target, target, *destination), std::logic_error);
	EXPECT_EQ(target->save(), beforeForeign);
	EXPECT_EQ(destination->forecastState()->unit(id)->focus->charges, 2);
}

TEST_F(ForecastNativeAdapterTest, UnacceptedFocusKeepsHardChargesAndExhaustedBudgetRejectsBeforeMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto attackers = parent->battleGetUnitsIf([](const battle::Unit * unit)
	{ return unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0); });
	ASSERT_EQ(attackers.size(), 1u);
	ASSERT_EQ(attackers.front()->creatureId(), CreatureID(CreatureID::decode("core:pikeman")));
	const auto id = attackers.front()->unitId();
	auto actor = parent->getForUpdate(id);
	ASSERT_FALSE(actor->isShooter());
	actor->waitedThisTurn = true;
	actor->addUnitBonus(newHorizonsFocusMagic::enchantment(*actor,
		SpellID(SpellID::decode(newHorizonsFocusMagic::SPELL))));
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto expected = reference->getForUpdate(id);
	focusMagicAI::afterAttack(*expected, true, false, true, false);
	focusMagicAI::afterAttack(*actor, true, false, true, false, parent.get());
	EXPECT_EQ(actor->save(), expected->save());
	EXPECT_EQ(actor->shots.available(), expected->shots.available());
	EXPECT_EQ(newHorizonsFocusMagic::charges(*actor), 3);
	EXPECT_EQ(parent->forecastState()->unit(id)->focus->charges, 3);
	EXPECT_EQ(parent->forecastState()->unit(id)->activation.waitBonusUsed, actor->battlecraftWaitBonusUsed);
	const auto before = actor->save();
	const auto revision = parent->forecastState()->revision();
	parent->setSearchBudget(std::make_shared<battleSearch::SearchBudget>(battleSearch::WorkAmounts{}));
	EXPECT_THROW(focusMagicAI::afterAttack(*actor, true, false, true, true, parent.get()), battleSearch::BudgetExhausted);
	EXPECT_EQ(actor->save(), before);
	EXPECT_EQ(newHorizonsFocusMagic::charges(*actor), 3);
	EXPECT_EQ(parent->forecastState()->revision(), revision);
}

TEST_F(ForecastNativeAdapterTest, FocusAcceptedRecastRefreshesEffectOnlyAndNeverNativeSpentAmmo)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, false, false, "core:archer"));
	const auto attackers = parent->battleGetUnitsIf([](const battle::Unit * unit)
	{ return unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0); });
	ASSERT_EQ(attackers.size(), 1u);
	ASSERT_EQ(attackers.front()->creatureId(), CreatureID(CreatureID::decode("core:archer")));
	const auto id = attackers.front()->unitId();
	auto root = parent->getForUpdate(id);
	ASSERT_TRUE(root->isShooter());
	const SpellID focus(SpellID::decode(newHorizonsFocusMagic::SPELL));
	root->addUnitBonus(newHorizonsFocusMagic::enchantment(*root, focus));
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *child);
	auto actor = child->getForUpdate(id);
	focusMagicAI::afterAttack(*actor, true, false, true, true, child.get());
	ASSERT_EQ(newHorizonsFocusMagic::charges(*actor), 2);
	const auto ammoAfterShot = actor->shots.available();
	const auto previous = newHorizonsFocusMagic::nativeRecords(*actor);
	const auto replacement = newHorizonsFocusMagic::enchantment(*actor, focus);
	ASSERT_TRUE(forecast::NativeAdapter::effectApplication(*child, id, previous, replacement));
	EXPECT_EQ(newHorizonsFocusMagic::charges(*actor), 3);
	EXPECT_EQ(child->forecastState()->unit(id)->focus->charges, 3);
	EXPECT_EQ(actor->shots.available(), ammoAfterShot);
	EXPECT_EQ(child->forecastState()->unit(id)->activation.shots.available, ammoAfterShot);
	EXPECT_EQ(newHorizonsFocusMagic::nativeRecords(*actor).size(), replacement.size());
	EXPECT_EQ(parent->forecastState()->unit(id)->focus->charges, 3);
}
