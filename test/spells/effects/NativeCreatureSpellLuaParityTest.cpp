/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NativeSpellLuaParityFixture.h"

namespace test
{
TEST_F(NativeSpellLuaParity, CabirRepairPreservesEligibleCasualtiesAndCompleteHealedState)
{
	const SpellID id(SpellID::decode("new-horizons:cabirRepair"));
	const CreatureID golem(CreatureID::decode("core:stoneGolem"));
	ASSERT_NE(id, SpellID::NONE);
	ASSERT_NE(golem, CreatureID::NONE);
	spell(id);
	auto & caster = unit(73);
	auto & recipient = unit(42);
	EXPECT_CALL(caster, creatureId()).Times(AnyNumber()).WillRepeatedly(Return(CreatureID(0)));
	EXPECT_CALL(recipient, creatureId()).Times(AnyNumber()).WillRepeatedly(Return(golem));
	EXPECT_CALL(recipient, unitType()).Times(AnyNumber()).WillRepeatedly(Return(golem.toCreature()));
	EXPECT_CALL(recipient, getCount()).Times(AnyNumber()).WillRepeatedly(Return(9));
	EXPECT_CALL(recipient, getFirstHPleft()).Times(AnyNumber()).WillRepeatedly(Return(50));
	EXPECT_CALL(recipient, getAvailableHealth()).Times(AnyNumber()).WillRepeatedly(Return(850));
	EXPECT_CALL(recipient, getTotalHealth()).Times(AnyNumber()).WillRepeatedly(Return(1200));
	EXPECT_CALL(recipient, getUnusableRemains()).Times(AnyNumber()).WillRepeatedly(Return(0));
	EXPECT_CALL(recipient, acquireState()).Times(AnyNumber()).WillRepeatedly(Invoke([this, &recipient]
	{
		auto state = std::make_shared<::battle::CUnitStateDetached>(&recipient, &recipient);
		state->localInit(&unitEnvironment);
		int64_t damage = 350;
		state->damage(damage);
		return state;
	}));
	EXPECT_CALL(mechanicsMock, getUnitCaster()).Times(AnyNumber()).WillRepeatedly(Return(&caster));
	EXPECT_CALL(mechanicsMock, getEffectValue()).Times(AnyNumber()).WillRepeatedly(Return(337));
	EXPECT_CALL(mechanicsMock, ownerMatches(_)).Times(AnyNumber()).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, isReceptive(_)).Times(AnyNumber()).WillRepeatedly(Return(true));
	prepare("cabirRepair", JsonNode(), "new-horizons");
	const Target target{::battle::Destination(&recipient)};
	ASSERT_TRUE(oracle->applicableTarget(problemMock, &mechanicsMock, target));
	ASSERT_TRUE(subject->applicableTarget(problemMock, &mechanicsMock, target));
	const auto expected = oracle->getHealthChange(&mechanicsMock, target);
	const auto actual = subject->getHealthChange(&mechanicsMock, target);
	EXPECT_EQ(actual.hpDelta, expected.hpDelta);
	EXPECT_EQ(actual.unitsDelta, expected.unitsDelta);
	EXPECT_GT(actual.unitsDelta, 0);
	expectApplyParity(target);
	ASSERT_FALSE(trace.packets.empty());
	EXPECT_EQ(trace.packets.front().first, "units");
}

TEST_F(NativeSpellLuaParity, GenieLuckPreservesBothCanonicalFamiliesAndRemovesOnlyPriorLuckSource)
{
	auto & caster = unit(73);
	auto & recipient = unit(42);
	EXPECT_CALL(mechanicsMock, getUnitCaster()).Times(AnyNumber()).WillRepeatedly(Return(&caster));
	EXPECT_CALL(mechanicsMock, ownerMatches(_)).Times(AnyNumber()).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, isReceptive(_)).Times(AnyNumber()).WillRepeatedly(Return(true));
	recipient.addNewBonus(std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::LUCK,
		BonusSource::SPELL_EFFECT, 3, BonusSourceID(SpellID(SpellID::FORTUNE))));
	recipient.addNewBonus(std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
		BonusSource::SPELL_EFFECT, 3, BonusSourceID(SpellID(SpellID::HASTE))));
	for(const auto * key : {"new-horizons:bestowLuck", "new-horizons:turnOfFate"})
	{
		SCOPED_TRACE(key);
		const SpellID id(SpellID::decode(key));
		ASSERT_NE(id, SpellID::NONE);
		spell(id);
		prepare("genieLuck", JsonNode(), "new-horizons");
		expectApplyParity({::battle::Destination(&recipient)});
		EXPECT_EQ(trace.packets.size(), 2u);
	}
}
}
