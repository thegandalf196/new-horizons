/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NativeSpellLuaParityFixture.h"

namespace test
{
TEST_F(NativeSpellLuaParity, TeleportPreservesTwoPhaseTargetTypesAndExactMovementPacket)
{
	spell(SpellID::TELEPORT);
	auto & recipient = unit(42);
	EXPECT_CALL(mechanicsMock, isReceptive(_)).Times(AnyNumber()).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, isMassive()).Times(AnyNumber()).WillRepeatedly(Return(false));
	EXPECT_CALL(mechanicsMock, isSmart()).Times(AnyNumber()).WillRepeatedly(Return(false));
	EXPECT_CALL(mechanicsMock, alwaysHitFirstTarget()).Times(AnyNumber()).WillRepeatedly(Return(false));
	prepare("teleport", JsonNode());
	for(const auto & input : {std::vector<AimType>{}, std::vector<AimType>{AimType::CREATURE},
		std::vector<AimType>{AimType::LOCATION}, std::vector<AimType>{AimType::CREATURE, AimType::LOCATION}})
	{
		auto expected = input, actual = input;
		oracle->adjustTargetTypes(expected, &mechanicsMock);
		subject->adjustTargetTypes(actual, &mechanicsMock);
		EXPECT_EQ(actual, expected);
	}
	const Target aim{::battle::Destination(&recipient), ::battle::Destination(BattleHex(77))};
	const Target first{::battle::Destination(&recipient)};
	EXPECT_EQ(subject->transformTarget(&mechanicsMock, aim, first), oracle->transformTarget(&mechanicsMock, aim, first));
	expectApplyParity(aim);
	ASSERT_EQ(trace.packets.size(), 1u);
	EXPECT_EQ(trace.packets.front().first, "movement");
}

TEST_F(NativeSpellLuaParity, PurifyPreservesActionCenterAndLeavesGroupMutationToAuthoritativePath)
{
	prepare("purify", JsonNode());
	const Target center{::battle::Destination(BattleHex(77))};
	for(const auto & target : {Target{}, center, Target{center.front(), ::battle::Destination(BattleHex(78))}})
		EXPECT_EQ(subject->applicableTarget(problemMock, &mechanicsMock, target),
			oracle->applicableTarget(problemMock, &mechanicsMock, target));
	EXPECT_EQ(subject->transformTarget(&mechanicsMock, center, {}), oracle->transformTarget(&mechanicsMock, center, {}));
	const auto expected = run(oracle, center), actual = run(subject, center);
	EXPECT_EQ(actual, expected);
	EXPECT_TRUE(actual.packets.empty());
	EXPECT_TRUE(actual.random.empty());
}
}
