/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/FlatDamageInputs.h"
#include "../../lib/combatScripts/NativeDamageFacts.h"
#include "../../lib/CRandomGenerator.h"
#include <stdexcept>

namespace
{
class FlatDamageInputsTest : public ProjectedTargetHitFixture
{
protected:
	void prepareOrdinary()
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		// Angels carry pair-specific native Hatred; use an admitted ordinary
		// target rather than silently widening the pilot domain for this test.
		braced = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(94), 30);
		resetChild();
	}
	void compare(bool shooting)
	{
		auto actor = child->getForUpdate(incoming->unitId());
		auto target = child->getForUpdate(braced->unitId());
		FlatDamageInputs inputs(*child, shooting);
		const battle::Units targets{target.get()};
		ASSERT_TRUE(inputs.prepare(*actor, {targets.data(), targets.size()}));
		for(const auto luck : {-1, 0, 1})
		{
			BattleAttackInfo attack(actor.get(), target.get(), 0, shooting);
			attack.luckyStrike = luck > 0;
			attack.unluckyStrike = luck < 0;
			const auto expected = child->calculateDmgRange(attack);
			const auto expectedAverage = child->battleExpectedLuckDamage(attack);
			FlatDamageInputs::Scope scope(*child, &inputs);
			ASSERT_NE(child->preparedUnitDamageFacts(*actor, shooting), nullptr);
			const auto actual = child->calculateDmgRange(attack);
			EXPECT_EQ(actual.damage.min, expected.damage.min);
			EXPECT_EQ(actual.damage.max, expected.damage.max);
			EXPECT_EQ(actual.kills.min, expected.kills.min);
			EXPECT_EQ(actual.kills.max, expected.kills.max);
			EXPECT_EQ(actual.damageBeforeDefense.min, expected.damageBeforeDefense.min);
			EXPECT_EQ(actual.damageBeforeDefense.max, expected.damageBeforeDefense.max);
			EXPECT_EQ(child->battleExpectedLuckDamage(attack), expectedAverage);
		}
		EXPECT_EQ(child->preparedUnitDamageFacts(*actor, shooting), nullptr);
	}
};
}

TEST_F(FlatDamageInputsTest, OrdinaryMeleeAndRangedRetainNumericalKernelAndRng)
{
	ASSERT_NO_FATAL_FAILURE(prepareOrdinary());
	const auto gameRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto serverRng = projectedHitTest::serialized(*gameHandler->randomizer);
	ASSERT_NO_FATAL_FAILURE(compare(false));
	ASSERT_NO_FATAL_FAILURE(compare(true));
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverRng);
}

TEST_F(FlatDamageInputsTest, SiblingsAndWrongAttackModeCannotBorrowFactsAndScopesRestoreOnThrow)
{
	ASSERT_NO_FATAL_FAILURE(prepareOrdinary());
	auto actor = child->getForUpdate(incoming->unitId());
	auto target = child->getForUpdate(braced->unitId());
	FlatDamageInputs inputs(*child, false);
	const battle::Units targets{target.get()};
	ASSERT_TRUE(inputs.prepare(*actor, {targets.data(), targets.size()}));
	EXPECT_EQ(inputs.find(*actor, true), nullptr);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), live);
	{
		FlatDamageInputs::Scope scope(*sibling, &inputs);
		EXPECT_EQ(sibling->preparedUnitDamageFacts(*sibling->getForUpdate(incoming->unitId()), false), nullptr);
	}
	try
	{
		FlatDamageInputs::Scope scope(*child, &inputs);
		throw std::runtime_error("probe exit");
	}
	catch(const std::runtime_error &) {}
	EXPECT_EQ(child->preparedUnitDamageFacts(*actor, false), nullptr);
}

TEST_F(FlatDamageInputsTest, UnsupportedPairFamiliesRetainReferenceAndNextBatchSeesModifiers)
{
	ASSERT_NO_FATAL_FAILURE(prepareOrdinary());
	auto actor = child->getForUpdate(incoming->unitId());
	actor->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE, BonusType::REVENGE,
		BonusSource::OTHER, 1, BonusSourceID())});
	EXPECT_FALSE(nativeDamage::prepareUnit(*actor, false));
	actor->removeUnitBonus(Selector::type()(BonusType::REVENGE));
	ASSERT_NO_FATAL_FAILURE(compare(false));
}

TEST_F(FlatDamageInputsTest, NewBatchAfterVirtualModifierChangeUsesFreshDerivedFacts)
{
	ASSERT_NO_FATAL_FAILURE(prepareOrdinary());
	auto actor = child->getForUpdate(incoming->unitId());
	auto target = child->getForUpdate(braced->unitId());
	BattleAttackInfo attack(actor.get(), target.get(), 0, false);
	const auto before = child->calculateDmgRange(attack);
	ASSERT_NO_FATAL_FAILURE(compare(false));
	// End the preceding numeric frame BEFORE the branch changes. The next
	// selector batch must rebuild, even though the real battle did not change.
	actor->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE, BonusType::GENERAL_DAMAGE_PREMY,
		BonusSource::OTHER, 100, BonusSourceID())});
	const auto after = child->calculateDmgRange(attack);
	EXPECT_GT(after.damage.max, before.damage.max);
	ASSERT_NO_FATAL_FAILURE(compare(false));
}

TEST_F(FlatDamageInputsTest, MixedRosterPreparationKeepsUnsupportedUnitsOnReferenceInBothModes)
{
	ASSERT_NO_FATAL_FAILURE(prepareOrdinary());
	const auto * native = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(90), 10);
	resetChild();
	auto unsupported = child->getForUpdate(native->unitId());
	unsupported->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE, BonusType::REVENGE,
		BonusSource::OTHER, 1, BonusSourceID())});
	auto actor = child->getForUpdate(incoming->unitId());
	auto target = child->getForUpdate(braced->unitId());
	FlatDamageInputs inputs(*child, false);
	ASSERT_TRUE(inputs.prepareRoster(*child));
	EXPECT_EQ(inputs.find(*unsupported, false), nullptr);
	EXPECT_EQ(inputs.find(*unsupported, true), nullptr);
	ASSERT_NE(inputs.find(*actor, false), nullptr);
	ASSERT_NE(inputs.find(*actor, true), nullptr);
	for(const bool shooting : {false, true})
	{
		BattleAttackInfo attack(actor.get(), target.get(), 0, shooting);
		const auto expected = child->calculateDmgRange(attack);
		FlatDamageInputs::Scope scope(*child, &inputs);
		const auto actual = child->calculateDmgRange(attack);
		EXPECT_EQ(actual.damage.min, expected.damage.min);
		EXPECT_EQ(actual.damage.max, expected.damage.max);
	}
}
