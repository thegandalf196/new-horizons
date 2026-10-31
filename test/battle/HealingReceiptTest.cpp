/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/HealingReceipt.h"
#include <limits>
#include <stdexcept>
TEST(HealingReceipt, PreventedHealthFloorsAfterEligibleCapacity)
{
	EXPECT_EQ(battle::receivedHealing(10, 5000), 5);
	EXPECT_EQ(battle::receivedHealing(1, 5000), 1);
	EXPECT_EQ(battle::receivedHealing(3, 5000), 2);
	EXPECT_EQ(battle::receivedHealing(9, 5000), 5);
	EXPECT_EQ(battle::receivedHealing(10, 0), 10);
	EXPECT_EQ(battle::receivedHealing(10, 10'000), 0);
	// The kernel receives capped H, never the producer's raw1000 request.
	EXPECT_NE(battle::receivedHealing(10, 5000), battle::receivedHealing(1000, 5000));
}
TEST(HealingReceipt, SmallDomainAgreesWithExactIntegerDefinitionAndIsMonotone)
{
	for(int64_t health = 0; health <= 257; ++health)
	{
		int64_t previous = health;
		for(int32_t reduction = 0; reduction <= 10'000; ++reduction)
		{
			const auto actual = battle::receivedHealing(health, reduction);
			ASSERT_EQ(actual, health - health * reduction / 10'000);
			ASSERT_GE(actual, 0);
			ASSERT_LE(actual, previous);
			previous = actual;
		}
	}
}
TEST(HealingReceipt, FullWidthInputsDoNotOverflow)
{
	constexpr auto maximum = std::numeric_limits<int64_t>::max();
	EXPECT_EQ(battle::receivedHealing(maximum, 0), maximum);
	EXPECT_EQ(battle::receivedHealing(maximum, 10'000), 0);
	EXPECT_EQ(battle::receivedHealing(maximum, 5000), 4'611'686'018'427'387'904LL);
	EXPECT_EQ(battle::receivedHealing(maximum, 9999), 922'337'203'685'478LL);
	EXPECT_EQ(battle::receivedHealing(maximum, 1), 9'222'449'699'651'090'330LL);
}
TEST(HealingReceipt, InvalidInputsFailClosedWithoutChangingTheDomain)
{
	EXPECT_THROW(battle::receivedHealing(-1, 0), std::invalid_argument);
	EXPECT_THROW(battle::receivedHealing(1, -1), std::invalid_argument);
	EXPECT_THROW(battle::receivedHealing(1, 10'001), std::invalid_argument);
}
