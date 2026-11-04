/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../client/windows/HeroTemporaryEffects.h"

TEST(HeroTemporaryEffects, PermanentBonusesAndExpiredCountersAreOmitted)
{
	using namespace heroTemporaryEffects;
	EXPECT_FALSE(visibleDuration(BonusDuration::PERMANENT, 0));
	EXPECT_FALSE(visibleDuration(0, 7));
	EXPECT_FALSE(visibleDuration(BonusDuration::N_DAYS, 0));
	EXPECT_FALSE(visibleDuration(BonusDuration::N_TURNS, -1));
	EXPECT_TRUE(visibleDuration(BonusDuration::N_DAYS, 1));
	EXPECT_TRUE(visibleDuration(BonusDuration::ONE_BATTLE, 0));
	EXPECT_TRUE(visibleDuration(BonusDuration::ONE_WEEK, 0));
}

TEST(HeroTemporaryEffects, AdventureExpiryReflectsActualRemainingDuration)
{
	using namespace heroTemporaryEffects;
	EXPECT_EQ(expiry(BonusDuration::ONE_WEEK, 0), "until the end of this week");
	EXPECT_EQ(expiry(BonusDuration::ONE_BATTLE, 0), "until the end of the next battle");
	EXPECT_EQ(expiry(BonusDuration::N_DAYS, 1), "1 day remaining");
	EXPECT_EQ(expiry(BonusDuration::N_DAYS, 3), "3 days remaining");
	EXPECT_EQ(expiry(BonusDuration::ONE_DAY | BonusDuration::ONE_BATTLE, 0),
		"until the end of the next battle or until the end of today");
}

TEST(HeroTemporaryEffects, CreatureActivationAndAttackLifetimesStayDistinct)
{
	using namespace heroTemporaryEffects;
	EXPECT_EQ(expiry(BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION, 0), "until its next creature activation begins");
	EXPECT_EQ(expiry(BonusDuration::STACK_ACTIVATION, 0), "through its next creature activation");
	EXPECT_EQ(expiry(BonusDuration::UNTIL_ATTACK, 0), "until an attack or retaliation");
	EXPECT_EQ(expiry(BonusDuration::UNTIL_OWN_ATTACK, 0), "until its own attack");
}
