/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../client/windows/CreatureDamagePresentation.h"

TEST(CreatureDamagePresentation, AttunedWaterElementalShowsBaseAndBoostedRanges)
{
	// Owner's example: Water Elemental 7-10 at Basic Attunement +20%.
	const auto readout = creatureDamagePresentation::attuned(7, 10, 20, false);
	EXPECT_EQ(readout.value, "7 - 10 (8.4 - 12)");
	EXPECT_NE(readout.help.find("+20% melee attack damage"), std::string::npos);
	EXPECT_NE(readout.help.find("before the target's Defense"), std::string::npos);
}

TEST(CreatureDamagePresentation, BoostedAmountsUseOneDecimalAndDropWholeSuffix)
{
	EXPECT_EQ(creatureDamagePresentation::boostedAmount(10, 20), "12");
	EXPECT_EQ(creatureDamagePresentation::boostedAmount(7, 20), "8.4");
	EXPECT_EQ(creatureDamagePresentation::boostedAmount(30, 25), "37.5");
	EXPECT_EQ(creatureDamagePresentation::boostedAmount(3, 15), "3.5"); // 3.45 rounds to one decimal
}

TEST(CreatureDamagePresentation, FixedDamageShowsSingleValues)
{
	const auto readout = creatureDamagePresentation::attuned(5, 5, 20, true);
	EXPECT_EQ(readout.value, "5 (6)");
	EXPECT_NE(readout.help.find("ranged attack damage"), std::string::npos);
}
