/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NativeSpellLuaParityFixture.h"

namespace test
{
TEST_F(NativeSpellLuaParity, HealPreservesWoundsCasualtiesAndResurrectionPowerInCompleteUnitPackets)
{
	spell(SpellID::CURE);
	auto & recipient = unit(42);
	auto & caster = unit(73);
	for(auto * unit : {&recipient, &caster})
	{
		EXPECT_CALL(*unit, creatureId()).Times(AnyNumber()).WillRepeatedly(Return(CreatureID(0)));
		EXPECT_CALL(*unit, unitType()).Times(AnyNumber()).WillRepeatedly(Return(CreatureID(0).toCreature()));
	}
	EXPECT_CALL(mechanicsMock, getUnitCaster()).Times(AnyNumber()).WillRepeatedly(Return(&caster));
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
	EXPECT_CALL(mechanicsMock, getEffectValue()).Times(AnyNumber()).WillRepeatedly(Return(337));
	EXPECT_CALL(mechanicsMock, isReceptive(_)).Times(AnyNumber()).WillRepeatedly(Return(true));
	const Target target{::battle::Destination(&recipient)};
	for(const auto * level : {"heal", "resurrect", "overHeal"})
		for(const auto * power : {"permanent", "oneBattle"})
		{
			SCOPED_TRACE(level);
			SCOPED_TRACE(power);
			JsonNode config;
			config["healLevel"].String() = level;
			config["healPower"].String() = power;
			prepare("heal", config);
			const auto expectedPreview = oracle->getHealthChange(&mechanicsMock, target);
			const auto actualPreview = subject->getHealthChange(&mechanicsMock, target);
			EXPECT_EQ(actualPreview.hpDelta, expectedPreview.hpDelta);
			EXPECT_EQ(actualPreview.unitsDelta, expectedPreview.unitsDelta);
			EXPECT_EQ(actualPreview.unitType, expectedPreview.unitType);
			// CUnitState rejects temporary ordinary wound healing; resurrection
			// and Overheal have their distinct supported ONE_BATTLE semantics.
			if(std::string_view(level) == "heal" && std::string_view(power) == "oneBattle")
			{
				EXPECT_EQ(actualPreview.hpDelta, 0);
				EXPECT_EQ(actualPreview.unitsDelta, 0);
			}
			else EXPECT_GT(actualPreview.hpDelta, 0);
			expectApplyParity(target);
			ASSERT_FALSE(trace.packets.empty());
			EXPECT_EQ(trace.packets.front().first, "units");
		}
}
}
