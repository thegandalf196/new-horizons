/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "EffectFixture.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsSorcerySpells.h"
#include "../../../lib/battle/NewHorizonsFocusMagic.h"
#include "../../../lib/battle/AccessibilityInfo.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/NewHorizonsFrozen.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../mock/mock_UnitEnvironment.h"

namespace test
{
namespace
{
class PlacementCallback final : public CBattleInfoCallback
{
public:
	AccessibilityInfo board;
	const IBattleInfo * getBattle() const override { return nullptr; }
	std::optional<PlayerColor> getPlayerID() const override { return std::nullopt; }
	AccessibilityInfo getAccessibility() const override { return board; }
};

class FrozenBattleView final : public battle::BattleFake
{
	JsonNode rules{JsonPath::builtin("config/newHorizonsMagic")};
public:
	const JsonNode & getMagicRules() const override { return rules; }
};

class NewHorizonsSorceryEffects : public Test, public EffectFixture
{
protected:
	NiceMock<UnitEnvironmentMock> environment;
	SpellID focus = SpellID::NONE;
	NewHorizonsSorceryEffects() : EffectFixture("core:damage") {}
	void SetUp() override
	{
		EffectFixture::setUp();
		focus = SpellID(SpellID::decode(newHorizonsFocusMagic::SPELL));
		ASSERT_NE(focus, SpellID::NONE);
	}

	battle::UnitFake & unit(uint32_t id = 42)
	{
		auto & result = unitsFake.add(BattleSide::DEFENDER);
		result.expectAnyBonusSystemCall();
		EXPECT_CALL(result, unitId()).WillRepeatedly(Return(id));
		EXPECT_CALL(result, alive()).WillRepeatedly(Return(true));
		EXPECT_CALL(result, isGhost()).WillRepeatedly(Return(false));
		EXPECT_CALL(result, isInvincible()).WillRepeatedly(Return(false));
		EXPECT_CALL(result, isValidTarget(_)).WillRepeatedly(Return(true));
		return result;
	}

	void expectForceLanceCast()
	{
		const SpellID spell(SpellID::decode(newHorizonsSorcerySpells::FORCE_LANCE));
		ASSERT_NE(spell, SpellID::NONE);
		const auto * definition = spell.toSpell();
		ASSERT_NE(definition, nullptr);
		ASSERT_EQ(definition->getJsonKey(), newHorizonsSorcerySpells::FORCE_LANCE);
		ASSERT_TRUE(definition->isDamage());
		ASSERT_TRUE(definition->isMagical());
		EXPECT_CALL(mechanicsMock, getSpell()).WillRepeatedly(Return(definition));
		EXPECT_CALL(mechanicsMock, getSpellId()).WillRepeatedly(Return(spell));
		EXPECT_CALL(mechanicsMock, isMagicalEffect()).WillRepeatedly(Return(definition->isMagical()));
		EXPECT_CALL(mechanicsMock, getUnitCaster()).WillRepeatedly(Return(nullptr));
	}

	void add(battle::UnitFake & unit, const std::vector<Bonus> & bonuses)
	{
		for(const auto & bonus : bonuses) unit.addNewBonus(std::make_shared<Bonus>(bonus));
	}
};
}

TEST_F(NewHorizonsSorceryEffects, FocusConvertsMeleeAndAddsOnlyExplicitRangedPerkAndSpecialtyPremiums)
{
	auto & target = unit();
	const auto bonuses = newHorizonsFocusMagic::enchantment(target, focus, 30);
	ASSERT_EQ(bonuses.size(), 3);
	EXPECT_TRUE(newHorizonsFocusMagic::isChargeMarker(bonuses[0]));
	EXPECT_EQ(bonuses[0].val, 3);
	EXPECT_EQ(bonuses[1].type, BonusType::SHOOTER);
	EXPECT_EQ(bonuses[2].type, BonusType::PERCENTAGE_DAMAGE_BOOST);
	EXPECT_EQ(bonuses[2].subtype, BonusSubtypeID(BonusCustomSubtype::damageTypeRanged));
	EXPECT_EQ(bonuses[2].val, 30);
	add(target, bonuses);
	EXPECT_TRUE(newHorizonsFocusMagic::converted(target));
	// Recasting a converted unit must not reclassify its temporary SHOOTER as native.
	const auto recast = newHorizonsFocusMagic::enchantment(target, focus, 30);
	EXPECT_EQ(recast[0].val, 3);
	EXPECT_EQ(recast[1].type, BonusType::SHOOTER);
	ASSERT_EQ(recast.size(), 3);
	EXPECT_EQ(recast[2].val, 30);
}

TEST_F(NewHorizonsSorceryEffects, FocusEnhancesNativeShooterWithZeroValuedShooterBonusOnlyAtRange)
{
	auto & target = unit();
	target.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SHOOTER,
		BonusSource::CREATURE_ABILITY, 0, BonusSourceID()));
	const auto bonuses = newHorizonsFocusMagic::enchantment(target, focus, 30);
	ASSERT_EQ(bonuses.size(), 2);
	EXPECT_EQ(bonuses[1].type, BonusType::PERCENTAGE_DAMAGE_BOOST);
	EXPECT_EQ(bonuses[1].subtype, BonusSubtypeID(BonusCustomSubtype::damageTypeRanged));
	EXPECT_EQ(bonuses[1].val, 50);
}

TEST_F(NewHorizonsSorceryEffects, FocusConsumesHardChargesAndRemovesOnlyItsOwnBundleOnLastStrike)
{
	for(int remaining = 3; remaining >= 1; --remaining)
	{
		auto & target = unit(40 + remaining);
		auto bonuses = newHorizonsFocusMagic::enchantment(target, focus);
		bonuses.front().val = remaining;
		add(target, bonuses);
		target.addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::SHOTS,
			BonusSource::SPELL_EFFECT, 1000, BonusSourceID(SpellID(SpellID::BLESS))));
		const auto consumed = newHorizonsFocusMagic::consume(target);
		EXPECT_EQ(newHorizonsFocusMagic::charges(target), remaining) << "Planning must not mutate its source";
		ASSERT_EQ(consumed.remove.size(), remaining == 1 ? 2 : 1);
		ASSERT_EQ(consumed.add.size(), remaining == 1 ? 0 : 1);
		if(!consumed.add.empty()) EXPECT_EQ(consumed.add[0].val, remaining - 1);
		for(const auto & removed : consumed.remove) EXPECT_EQ(removed.sid, BonusSourceID(focus));
	}
}

TEST_F(NewHorizonsSorceryEffects, NativeFocusRecastEmitsOneReplacementWithExactlyThreeCharges)
{
	auto & target = unit();
	auto previous = newHorizonsFocusMagic::enchantment(target, focus);
	previous[0].val = 1;
	add(target, previous);
	subject = nativeSpells::createFocusEnchantmentEffect("spells/focusMagicEnchantment");
	ASSERT_TRUE(subject);
	subject->init(JsonNode());
	EXPECT_CALL(mechanicsMock, ownerMatches(&target)).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, isNegativeSpell()).WillRepeatedly(Return(false));
	EXPECT_CALL(mechanicsMock, isReceptive(&target)).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, getSpellId()).WillRepeatedly(Return(focus));
	EXPECT_CALL(serverMock, apply(Matcher<SetStackEffect &>(_))).WillOnce(Invoke([&](SetStackEffect & packet)
	{
		ASSERT_EQ(packet.toRemove.size(), 1);
		EXPECT_EQ(packet.toRemove[0].second.size(), 2);
		ASSERT_EQ(packet.toAdd.size(), 1);
		ASSERT_EQ(packet.toAdd[0].second.size(), 2);
		EXPECT_EQ(packet.toAdd[0].second[0].val, 3);
		EXPECT_EQ(packet.toAdd[0].second[1].type, BonusType::SHOOTER);
	}));
	subject->apply(&serverMock, &mechanicsMock, {::battle::Destination(&target)});
}

TEST_F(NewHorizonsSorceryEffects, RecallUsesWholeDefenderFootprintAndIgnoresOwnOccupancy)
{
	auto & target = unit();
	EXPECT_CALL(target, getPosition()).WillRepeatedly(Return(BattleHex(8, 4)));
	EXPECT_CALL(target, unitSide()).WillRepeatedly(Return(BattleSide::DEFENDER));
	EXPECT_CALL(target, doubleWide()).WillRepeatedly(Return(true));
	PlacementCallback callback;
	callback.board.fill(EAccessibility::OBSTACLE);
	const BattleHex anchor(2, 4);
	// A defender's tail is to the right. The free left cell cannot complete the
	// anchor's footprint, but it is itself a legal nearer front with this tail.
	callback.board[anchor.toInt()] = EAccessibility::ACCESSIBLE;
	const BattleHex nearerFront(1, 4);
	callback.board[nearerFront.toInt()] = EAccessibility::ACCESSIBLE;
	const BattleHex fallback(5, 4);
	callback.board[fallback.toInt()] = EAccessibility::ACCESSIBLE;
	callback.board[BattleHex(6, 4).toInt()] = EAccessibility::ACCESSIBLE;
	EXPECT_EQ(target.occupiedHex(anchor), BattleHex(3, 4));
	EXPECT_FALSE(callback.board.accessible(anchor, true, BattleSide::DEFENDER));
	ASSERT_TRUE(callback.board.accessible(nearerFront, true, BattleSide::DEFENDER));
	ASSERT_LT(BattleHex::getDistance(anchor, nearerFront), BattleHex::getDistance(anchor, fallback));
	EXPECT_EQ(newHorizonsSorcerySpells::forcedRecallDestination(callback, target, anchor), nearerFront);
	callback.board[nearerFront.toInt()] = EAccessibility::OBSTACLE;
	EXPECT_EQ(newHorizonsSorcerySpells::forcedRecallDestination(callback, target, anchor), fallback);
	const BattleHex overlappingFront(7, 4);
	callback.board[overlappingFront.toInt()] = EAccessibility::ACCESSIBLE;
	callback.board[BattleHex(8, 4).toInt()] = EAccessibility::ALIVE_STACK;
	callback.board[BattleHex(9, 4).toInt()] = EAccessibility::ALIVE_STACK;
	EXPECT_FALSE(callback.board.accessible(overlappingFront, true, BattleSide::DEFENDER));
	EXPECT_EQ(newHorizonsSorcerySpells::forcedRecallDestination(callback, target, overlappingFront), overlappingFront);
	EXPECT_FALSE(newHorizonsSorcerySpells::forcedRecallDestination(callback, target, BattleHex(8, 4)));
}

TEST_F(NewHorizonsSorceryEffects, LanceAdmissionBypassesMagicalImmunityAndItsPinIsExactlyOneRound)
{
	auto & target = unit();
	subject = nativeSpells::createForceLanceEffect("spells/forceLance");
	ASSERT_TRUE(subject);
	subject->init(JsonNode());
	EXPECT_CALL(mechanicsMock, ownerMatches(&target)).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, isReceptive(_)).Times(0);
	const Target aim{::battle::Destination(&target)};
	EXPECT_EQ(subject->filterTarget(&mechanicsMock, aim).size(), 1);
	const auto pin = newHorizonsSorcerySpells::forceLancePin(SpellID(SpellID::MAGIC_ARROW));
	EXPECT_EQ(pin.type, BonusType::BIND_EFFECT);
	EXPECT_EQ(pin.duration, BonusDuration::N_TURNS);
	EXPECT_EQ(pin.turnsRemain, 1);
	EXPECT_FALSE(pin.parameters) << "No adjacency-bound producer or generic duration extension";
}

TEST_F(NewHorizonsSorceryEffects, LanceActualHitBypassesAbsorptionAndPreservesNormalRemains)
{
	auto & target = unit();
	EXPECT_CALL(target, unitBaseAmount()).WillRepeatedly(Return(2));
	EXPECT_CALL(target, getPosition()).WillRepeatedly(Return(BattleHex(40)));
	EXPECT_CALL(target, unitSide()).WillRepeatedly(Return(BattleSide::DEFENDER));
	EXPECT_CALL(target, doubleWide()).WillRepeatedly(Return(false));
	target.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH,
		BonusSource::CREATURE_ABILITY, 100, BonusSourceID()));
	std::shared_ptr<::battle::CUnitStateDetached> changed;
	EXPECT_CALL(target, acquireState()).WillRepeatedly(Invoke([&]
	{
		changed = std::make_shared<::battle::CUnitStateDetached>(&target, &target);
		changed->localInit(&environment);
		changed->health.addTemporaryHitPoints(500);
		changed->guardianSpiritHitPoints = 500;
		changed->guardianSpiritRoundsRemaining = 3;
		return changed;
	}));
	EXPECT_CALL(mechanicsMock, ownerMatches(&target)).WillRepeatedly(Return(true));
	int damage = 100;
	EXPECT_CALL(mechanicsMock, getEffectValue()).WillRepeatedly(Invoke([&] { return damage; }));
	ASSERT_NO_FATAL_FAILURE(expectForceLanceCast());
	const auto damageContext = mechanicsMock.spellDamageContext(target);
	EXPECT_EQ(damageContext.damageClass, ::battle::DamageClass::MAGICAL);
	EXPECT_EQ(damageContext.origin, ::battle::DamageOrigin::HERO_SPELL);
	EXPECT_CALL(serverMock, getRNG()).WillRepeatedly(Return(&rngMock));
	EXPECT_CALL(serverMock, describeChanges()).WillRepeatedly(Return(false));
	EXPECT_CALL(serverMock, apply(Matcher<StacksInjured &>(_))).Times(2).WillRepeatedly(Invoke([&](StacksInjured & packet)
	{
		ASSERT_EQ(packet.stacks.size(), 1);
		EXPECT_EQ(packet.stacks[0].damageAmount, damage);
		EXPECT_EQ(packet.stacks[0].killedAmount, damage / 100);
		EXPECT_EQ(static_cast<bool>(packet.stacks[0].flags & BattleStackAttacked::KILLED), damage == 200);
	}));
	EXPECT_CALL(serverMock, apply(Matcher<SetStackEffect &>(_))).WillOnce(Invoke([&](SetStackEffect & packet)
	{
		ASSERT_EQ(damage, 100) << "Only the surviving target receives a pin";
		ASSERT_EQ(packet.toUpdate.size(), 1);
		ASSERT_EQ(packet.toUpdate[0].second.size(), 1);
		EXPECT_EQ(packet.toUpdate[0].second[0].type, BonusType::BIND_EFFECT);
		EXPECT_EQ(packet.toUpdate[0].second[0].turnsRemain, 1);
	}));
	subject = nativeSpells::createForceLanceEffect("spells/forceLance");
	subject->init(JsonNode());
	for(const auto amount : {100, 200})
	{
		SCOPED_TRACE(amount);
		damage = amount;
		const auto preview = subject->getHealthChange(&mechanicsMock, {::battle::Destination(&target)});
		EXPECT_EQ(preview.hpDelta, -amount);
		EXPECT_EQ(preview.unitsDelta, -amount / 100);
		subject->apply(&serverMock, &mechanicsMock, {::battle::Destination(&target)});
		ASSERT_TRUE(changed);
		EXPECT_EQ(changed->health.getTemporaryHitPoints(), 500);
		EXPECT_EQ(changed->getCount(), 2 - amount / 100);
		if(amount == 100)
		{
			EXPECT_TRUE(changed->alive());
			EXPECT_EQ(changed->guardianSpiritHitPoints, 500);
			EXPECT_EQ(changed->guardianSpiritRoundsRemaining, 3);
		}
		else
		{
			// Legitimate death cleanup clears the Guardian pool, independently
			// of whether damage bypassed its absorption before the lethal hit.
			EXPECT_FALSE(changed->alive());
			EXPECT_EQ(changed->guardianSpiritHitPoints, 0);
			EXPECT_EQ(changed->guardianSpiritRoundsRemaining, 0);
		}
		EXPECT_EQ(changed->getUnusableRemains(), 0);
		EXPECT_FALSE(changed->ghostPending);
	}
}

TEST_F(NewHorizonsSorceryEffects, LanceActualMagicalHitClearsFrozenBeforeApplyingAnUnmodifiedPin)
{
	auto & target = unit();
	EXPECT_CALL(target, unitBaseAmount()).WillRepeatedly(Return(2));
	EXPECT_CALL(target, getPosition()).WillRepeatedly(Return(BattleHex(40)));
	EXPECT_CALL(target, unitSide()).WillRepeatedly(Return(BattleSide::DEFENDER));
	EXPECT_CALL(target, doubleWide()).WillRepeatedly(Return(false));
	target.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH,
		BonusSource::CREATURE_ABILITY, 100, BonusSourceID()));
	target.addNewBonus(std::make_shared<Bonus>(newHorizonsFrozen::makeFrozenMarker(BonusSourceID(CreatureID(0)), 1)));
	FrozenBattleView view;
	EXPECT_CALL(view, getUnitsIf(_)).WillRepeatedly(Invoke(&unitsFake, &battle::UnitsFake::getUnitsIf));
	EXPECT_CALL(view, getBattleID()).WillRepeatedly(Return(BattleID(0)));
	EXPECT_CALL(mechanicsMock, battle()).WillRepeatedly(Return(&view));
	EXPECT_CALL(mechanicsMock, ownerMatches(&target)).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, getEffectValue()).WillRepeatedly(Return(1));
	ASSERT_NO_FATAL_FAILURE(expectForceLanceCast());
	const auto damageContext = mechanicsMock.spellDamageContext(target);
	EXPECT_EQ(damageContext.damageClass, ::battle::DamageClass::MAGICAL);
	EXPECT_EQ(damageContext.origin, ::battle::DamageOrigin::HERO_SPELL);
	EXPECT_CALL(target, acquireState()).WillRepeatedly(Invoke([&]
	{
		auto changed = std::make_shared<::battle::CUnitStateDetached>(&target, &target);
		changed->localInit(&environment);
		changed->restoreFrozenApplicationRound(1);
		return changed;
	}));
	EXPECT_CALL(serverMock, getRNG()).WillRepeatedly(Return(&rngMock));
	EXPECT_CALL(serverMock, describeChanges()).WillRepeatedly(Return(false));
	InSequence order;
	EXPECT_CALL(serverMock, apply(Matcher<StacksInjured &>(_))).WillOnce(Invoke([](StacksInjured &) {}));
	EXPECT_CALL(serverMock, apply(Matcher<SetStackEffect &>(_))).WillOnce(Invoke([&](SetStackEffect & packet)
	{
		ASSERT_EQ(packet.toRemove.size(), 1);
		EXPECT_EQ(packet.toRemove[0].first, target.unitId());
		ASSERT_EQ(packet.toRemove[0].second.size(), 1);
		EXPECT_EQ(packet.toRemove[0].second[0].type, BonusType::PHYSICAL_AFFLICTION);
		EXPECT_TRUE(packet.toUpdate.empty());
	}));
	EXPECT_CALL(serverMock, apply(Matcher<SetStackEffect &>(_))).WillOnce(Invoke([&](SetStackEffect & packet)
	{
		ASSERT_EQ(packet.toUpdate.size(), 1);
		const auto & pin = packet.toUpdate[0].second.front();
		EXPECT_EQ(pin.type, BonusType::BIND_EFFECT);
		EXPECT_EQ(pin.turnsRemain, 1);
		EXPECT_EQ(pin.duration, BonusDuration::N_TURNS);
	}));
	subject = nativeSpells::createForceLanceEffect("spells/forceLance");
	subject->init(JsonNode());
	subject->apply(&serverMock, &mechanicsMock, {::battle::Destination(&target)});
}
}
