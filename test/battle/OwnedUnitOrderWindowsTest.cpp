/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/OwnedUnitOrderWindows.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/GameLibrary.h"
#include "../mock/mock_battle_Unit.h"
namespace
{
struct WithoutLibrary
{
	GameLibrary * old = LIBRARY;
	WithoutLibrary() { LIBRARY = nullptr; }
	~WithoutLibrary() { LIBRARY = old; }
};
HeroOrderState secondWind(bool active)
{
	HeroOrderState order; order.command = HeroCommand::SECOND_WIND;
	order.issuedRound = 1; order.primaryTargetUnitId = 7; order.secondWindActive = active;
	return order;
}
}
TEST(OwnedUnitOrderWindowsTest, UnknownAndKnownAbsenceRemainDistinctAndLazy)
{
	battle::OwnedUnitOrderWindows::Inputs inputs;
	inputs.at(BattleSide::DEFENDER).emplace();
	const auto windows = battle::OwnedUnitOrderWindows::fromOwnedInputs(inputs);
	testing::StrictMock<UnitMock> unit;
	EXPECT_CALL(unit, unitId()).Times(0);
	WithoutLibrary absent;
	EXPECT_THROW(windows->secondWindApplies(BattleSide::ATTACKER, unit), std::logic_error);
	EXPECT_FALSE(windows->secondWindApplies(BattleSide::DEFENDER, unit));
}
TEST(OwnedUnitOrderWindowsTest, CurrentAcceptedWindowChangesKeepParentAndSourceIndependent)
{
	battle::OwnedUnitOrderWindows::Inputs inputs;
	inputs.at(BattleSide::ATTACKER) = std::vector{secondWind(false)};
	const auto parent = battle::OwnedUnitOrderWindows::fromOwnedInputs(inputs);
	testing::StrictMock<UnitMock> unit;
	EXPECT_CALL(unit, unitId()).Times(3).WillRepeatedly(testing::Return(7));
	WithoutLibrary absent;
	EXPECT_FALSE(parent->secondWindApplies(BattleSide::ATTACKER, unit)); // Inactive does not read unitId.
	auto accepted = std::vector{secondWind(true)};
	const auto child = parent->withOrders(BattleSide::ATTACKER, accepted);
	accepted[0].secondWindActive = false;
	EXPECT_TRUE(child->secondWindApplies(BattleSide::ATTACKER, unit));
	EXPECT_FALSE(parent->secondWindApplies(BattleSide::ATTACKER, unit));
	const auto cleared = child->withOrders(BattleSide::ATTACKER, {});
	EXPECT_FALSE(cleared->secondWindApplies(BattleSide::ATTACKER, unit));
	EXPECT_TRUE(child->secondWindApplies(BattleSide::ATTACKER, unit));
	auto malformed = std::vector{secondWind(true), secondWind(true)};
	EXPECT_THROW(child->withOrders(BattleSide::ATTACKER, malformed), std::runtime_error);
	EXPECT_TRUE(child->secondWindApplies(BattleSide::ATTACKER, unit));
}
