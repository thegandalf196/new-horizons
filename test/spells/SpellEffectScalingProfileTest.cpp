/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/spells/SpellEffectScalingProfile.h"
#include <limits>
#include <stdexcept>
namespace
{
TEST(SpellEffectScalingProfileTest, WisdomRetainsOrdinaryMagnitudeSignedAndDurationRounding)
{
	const newHorizonsMetamagic::Profile profile{25};
	EXPECT_EQ(newHorizonsMetamagic::scaleMagnitude(101, profile), 126);
	EXPECT_EQ(newHorizonsMetamagic::scaleSignedBonus(-11, profile), -13);
	EXPECT_EQ(newHorizonsMetamagic::scaleDuration(3, profile), 3);
	EXPECT_EQ(newHorizonsMetamagic::scaleDuration(-1, profile), -1);
	EXPECT_EQ(newHorizonsMetamagic::scaleDuration(0, profile), 0);
}
TEST(SpellEffectScalingProfileTest, WisdomAndFactionAreAddedBeforeSingleOutputFloorAndCap)
{
	const newHorizonsMetamagic::Profile profile{25};
	EXPECT_EQ(newHorizonsMetamagic::spellDamageOutput(101, profile, 0, 15), 141);
	EXPECT_EQ(newHorizonsMetamagic::spellDamageOutput(101, profile, 130, 15), 130);
	EXPECT_EQ(newHorizonsMetamagic::spellDamageOutput(101, {}, 0), 101);
}
TEST(SpellEffectScalingProfileTest, SaturationDoesNotOverflowOrChangeValidOrdinaryValues)
{
	constexpr auto maximum = std::numeric_limits<int64_t>::max();
	EXPECT_EQ(newHorizonsMetamagic::scaleMagnitude(maximum, {25}), maximum);
	EXPECT_EQ(newHorizonsMetamagic::scaleMagnitude(maximum, {}), maximum);
	EXPECT_EQ(newHorizonsMetamagic::scaleSignedBonus(std::numeric_limits<int32_t>::min(), {1000}),
		std::numeric_limits<int32_t>::min());
	EXPECT_THROW(newHorizonsMetamagic::scaleMagnitude(1, {-1}), std::invalid_argument);
}
}
