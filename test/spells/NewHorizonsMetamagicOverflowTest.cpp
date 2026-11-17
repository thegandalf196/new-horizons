/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "../../lib/spells/NewHorizonsMetamagicOverflow.h"
#include <algorithm>
#include <limits>
using namespace newHorizonsMetamagic;
TEST(MetamagicOverflow, ExcessUsesRankPercentAndOneFinalIntegerFloor)
{
	EXPECT_EQ(overflowDamage(200, 80, 25), 30);
	EXPECT_EQ(overflowDamage(200, 80, 50), 60);
	EXPECT_EQ(overflowDamage(200, 80, 75), 90);
	EXPECT_EQ(overflowDamage(81, 80, 75), 0);
	EXPECT_EQ(overflowDamage(83, 80, 75), 2);
	for(const auto rankPercent : {0, 25, 50, 75})
	{
		EXPECT_EQ(overflowDamage(80, 80, rankPercent), 0);
		EXPECT_EQ(overflowDamage(79, 80, rankPercent), 0);
		EXPECT_EQ(overflowDamage(0, 80, rankPercent), 0);
	}
	EXPECT_EQ(overflowDamage(200, 0, 75), 0);
	EXPECT_EQ(overflowDamage(200, 80, -1), 0);
	EXPECT_EQ(overflowDamage(200, 80, 101), 0);
	const auto huge = std::numeric_limits<int64_t>::max() - 80;
	EXPECT_EQ(overflowDamage(std::numeric_limits<int64_t>::max(), 80, 75), huge / 100 * 75 + huge % 100 * 75 / 100);
}
TEST(MetamagicOverflow, StableNearestTieUsesMinimumOccupiedHexDistance)
{
	const BattleHexArray primary{BattleHex(7, 5), BattleHex(8, 5)};
	std::vector<OverflowCandidate> units{
		{1, {BattleHex(7, 5)}, true},
		{8, {BattleHex(10, 5)}, true},
		{7, {BattleHex(6, 5)}, true},
		{4, {BattleHex(9, 5), BattleHex(10, 5)}, true},
		{2, {BattleHex(8, 5)}, false}};
	// Both 7 and 4 are one hex from the original two-hex footprint; ID 4 wins.
	EXPECT_EQ(nearestOverflowEnemy(1, primary, units), 4u);
	std::ranges::reverse(units);
	EXPECT_EQ(nearestOverflowEnemy(1, primary, units), 4u);
	EXPECT_FALSE(nearestOverflowEnemy(1, {}, units));
	EXPECT_FALSE(nearestOverflowEnemy(1, primary, {}));
}
TEST(MetamagicOverflow, ActualPositiveLethalWitnessAndOneOriginalCastAreRequired)
{
	OverflowState state;
	state.primary = 4; state.percent = 25;
	state.observe({4, 80, 200, 80, false}); // Rebirth or other surviving primary.
	EXPECT_FALSE(state.lethal);
	state.observe({4, 80, 200, 0, true}); // Clone disappearance is not damaging HP.
	EXPECT_FALSE(state.lethal);
	state.observe({4, 80, 200, 80, true});
	ASSERT_TRUE(state.lethal);
	EXPECT_EQ(state.lethal->postShieldBeforeCasualties, 200);
	state.observe({4, 80, 200, 80, true});
	EXPECT_TRUE(state.invalid);
	OverflowState multi;
	multi.primary = 4; multi.percent = 25;
	multi.observe({5, 80, 200, 80, true});
	EXPECT_TRUE(multi.invalid); // An actual extra recipient invalidates whole-cast single-target proof.
	OverflowState consumed;
	consumed.primary = 4; consumed.percent = 25; consumed.consumed = true;
	consumed.observe({4, 80, 200, 80, true});
	EXPECT_FALSE(consumed.lethal);
}
