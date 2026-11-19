/*
 * MeleeResourceOrderTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "OriginalMeleeSequenceFixture.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/battle/StackEffectRemoval.h"

class MeleeResourceOrderTest : public OriginalMeleeSequenceTest
{
protected:
	std::shared_ptr<Bonus> aura(BonusDuration::Type duration, BonusType type = BonusType::PRIMARY_SKILL)
	{
		auto value = std::make_shared<Bonus>(duration, type, BonusSource::OTHER,
			type == BonusType::PRIMARY_SKILL ? 4 : 1, BonusSourceID(),
			type == BonusType::PRIMARY_SKILL ? BonusSubtypeID(PrimarySkill::ATTACK) : BonusSubtypeID());
		value->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::BATTLE_WIDE);
		if(type == BonusType::PRIMARY_SKILL)
			value->limiter = std::make_shared<CreatureLevelLimiter>();
		return value;
	}

	bool hasDelta(const battle::Unit & unit, const Bonus & expected)
	{
		return unit.hasBonus(CSelector([&expected](const Bonus * bonus)
		{
			return bonus->propagator && battle::matchesStackEffectRemoval(expected, *bonus);
		}));
	}

	void expectUnavailableBeforeInjury()
	{
		const auto before = known->battleGetUnitByID(incoming->unitId())->acquireState()->save().toString();
		const auto nativeBefore = incoming->acquireState()->save().toString();
		const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
		const auto nativeRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
		auto result = project();
		EXPECT_EQ(result.stop, heroOrderValue::MeleeSequenceStop::UNSUPPORTED_REACTION_POLICY);
		EXPECT_FALSE(result.meleeSequenceComplete());
		EXPECT_FALSE(result.activationComplete);
		EXPECT_FALSE(result.absoluteHorizonComplete);
		EXPECT_TRUE(result.hits.empty());
		ASSERT_TRUE(result.branch); // Permitted valid private movement prefix, not rollback.
		ASSERT_TRUE(result.incomingEntry);
		EXPECT_EQ(result.incomingEntry->voluntaryMovementDistance, 3);
		EXPECT_EQ(result.branch->battleGetUnitByID(incoming->unitId())->getPosition(), BattleHex(92));
		for(const auto * source : {incoming, braced})
			EXPECT_EQ(result.branch->battleGetUnitByID(source->unitId())->getAvailableHealth(), source->getAvailableHealth());
		EXPECT_EQ(known->battleGetUnitByID(incoming->unitId())->acquireState()->save().toString(), before);
		EXPECT_EQ(incoming->acquireState()->save().toString(), nativeBefore);
		EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
		EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), nativeRng);
	}
};

TEST_F(MeleeResourceOrderTest, NativeBattleWideVictimExpiryPreservesPrePacketUnlimitedCounterConsumption)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, true));
	releaseKnown();
	auto unlimited = aura(BonusDuration::UNTIL_BEING_ATTACKED, BonusType::UNLIMITED_RETALIATIONS);
	incoming->addNewBonus(unlimited);
	for(const auto * unit : {incoming, braced})
		ASSERT_TRUE(vstd::contains(*unit->getUnstackedBonuses(Selector::all), unlimited));
	ASSERT_FALSE(braced->counterAttacks.isLimited());
	ASSERT_NO_FATAL_FAILURE(refreshKnown());
	auto result = project();
	ASSERT_TRUE(result.meleeSequenceComplete());
	ASSERT_EQ(result.hits.size(), 2);
	EXPECT_EQ(result.hits.back().kind, heroOrderValue::MeleeStrikeKind::ORDINARY_RETALIATION);
	EXPECT_FALSE(result.branch->battleGetUnitByID(braced->unitId())->hasBonusOfType(BonusType::UNLIMITED_RETALIATIONS));
	const auto frozen = freezeOracle(result);
	const auto predicted = std::find_if(frozen.units.begin(), frozen.units.end(),
		[this](const auto & unit) { return unit.id == braced->unitId(); });
	ASSERT_NE(predicted, frozen.units.end());
	ASSERT_TRUE(predicted->saved["state"].isStruct());
	EXPECT_EQ(predicted->counters, 1);
	const auto used = predicted->saved["state"]["counterAttacks"]["used"].Integer();
	EXPECT_EQ(used, 0); // Omitted zero is the canonical CAmmo JSON default.
	retireKnown(result);
	ASSERT_NO_FATAL_FAILURE(compareNativeFrozen(frozen));
	const auto & packet = server.attacks.back();
	ASSERT_TRUE(packet.counter());
	ASSERT_EQ(packet.stackAttacking, braced->unitId());
	const auto actual = std::find_if(packet.attackerChanges.changedStacks.begin(), packet.attackerChanges.changedStacks.end(),
		[this](const auto & change) { return change.id == braced->unitId(); });
	ASSERT_NE(actual, packet.attackerChanges.changedStacks.end());
	ASSERT_TRUE(actual->data["state"].isStruct());
	EXPECT_EQ(actual->data["state"]["counterAttacks"]["used"].Integer(), used);
	EXPECT_EQ(actual->data["state"]["counterAttacks"]["used"].Integer(), 0);
}

TEST_F(MeleeResourceOrderTest, NativeNonExpiringBattleWideAuraRemainsAdmittedOnBothRecipients)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, true));
	releaseKnown();
	const int initialAttack = braced->getAttack(false);
	auto native = aura(BonusDuration::ONE_BATTLE);
	incoming->addNewBonus(native);
	for(const auto * unit : {incoming, braced})
		ASSERT_TRUE(vstd::contains(*unit->getUnstackedBonuses(Selector::all), native));
	ASSERT_EQ(braced->getAttack(false), initialAttack + 4); // Not filtered hero A/D.
	ASSERT_NO_FATAL_FAILURE(refreshKnown());
	auto result = project();
	ASSERT_TRUE(result.meleeSequenceComplete());
	for(const auto * unit : {incoming, braced})
		EXPECT_TRUE(vstd::contains(*result.branch->battleGetUnitByID(unit->unitId())->getUnstackedBonuses(Selector::all), native));
	ASSERT_NO_FATAL_FAILURE(compareNative(result));
	EXPECT_TRUE(vstd::contains(incoming->getExportedBonusList(), native));
}

TEST_F(MeleeResourceOrderTest, NonExpiringLegacyPropagatedAddHasNoCoherentInitialRecipientWorld)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, true));
	ASSERT_NO_FATAL_FAILURE(refreshLegacyKnown());
	const auto local = aura(BonusDuration::ONE_BATTLE);
	known->getForUpdate(incoming->unitId())->addUnitBonus({*local});
	ASSERT_TRUE(hasDelta(*known->battleGetUnitByID(incoming->unitId()), *local));
	ASSERT_FALSE(hasDelta(*known->battleGetUnitByID(braced->unitId()), *local));
	ASSERT_NO_FATAL_FAILURE(expectUnavailableBeforeInjury());
}

TEST_F(MeleeResourceOrderTest, NonExpiringLegacyPropagatedRemovalHasNoCoherentInitialRecipientWorld)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, true));
	releaseKnown();
	const auto native = aura(BonusDuration::ONE_BATTLE);
	incoming->addNewBonus(native);
	for(const auto * unit : {incoming, braced})
		ASSERT_TRUE(vstd::contains(*unit->getUnstackedBonuses(Selector::all), native));
	ASSERT_NO_FATAL_FAILURE(refreshLegacyKnown());
	known->getForUpdate(incoming->unitId())->removeUnitBonus(std::vector<Bonus>{*native});
	ASSERT_FALSE(vstd::contains(*known->battleGetUnitByID(incoming->unitId())->getUnstackedBonuses(Selector::all), native));
	ASSERT_TRUE(vstd::contains(*known->battleGetUnitByID(braced->unitId())->getUnstackedBonuses(Selector::all), native));
	ASSERT_NO_FATAL_FAILURE(expectUnavailableBeforeInjury());
}

TEST_F(MeleeResourceOrderTest, NonExpiringLegacyPropagatedUpdateCannotMasqueradeAsANativeExport)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, true));
	ASSERT_NO_FATAL_FAILURE(refreshLegacyKnown());
	known->getForUpdate(incoming->unitId())->updateUnitBonus({*aura(BonusDuration::ONE_BATTLE)});
	ASSERT_FALSE(known->getForUpdate(incoming->unitId())->bonusesToUpdate.empty());
	ASSERT_NO_FATAL_FAILURE(expectUnavailableBeforeInjury());
}

TEST_F(MeleeResourceOrderTest, NonExpiringPropagatedValueCopyCannotEraseTheInitialWorldGap)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, true));
	ASSERT_NO_FATAL_FAILURE(refreshLegacyKnown());
	auto copied = aura(BonusDuration::N_TURNS);
	copied->source = BonusSource::HERO_SPECIAL;
	copied->turnsRemain = 2;
	auto parent = known->getForUpdate(incoming->unitId());
	parent->addUnitBonus({*copied});
	parent->removeUnitBonus(Bonus::UntilAttack); // Captures HERO_SPECIAL clocks by value.
	ASSERT_TRUE(parent->bonusesToAdd.empty());
	ASSERT_TRUE(hasDelta(*parent, *copied));
	ASSERT_NO_FATAL_FAILURE(expectUnavailableBeforeInjury());
}
