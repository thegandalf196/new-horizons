/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/spells/NewHorizonsRunicReflection.h"
#include "../../lib/spells/effects/native/Damage.h"
#include "../../lib/spells/effects/native/LifeDrain.h"
#include <array>

namespace runic = newHorizonsRunicReflection;
namespace
{
std::array<int, 3> outcomes(int total, int native, bool legal = true)
{
	std::array<int, 3> result{};
	for(int sample = 0; sample != 100; ++sample)
		++result.at(static_cast<size_t>(runic::outcome(total, native, sample, legal)));
	return result;
}
}
TEST(RunicReflectionPolicyTest, NativeDwarfAndBattleDwarfReplaceRatherThanAddResistance)
{
	EXPECT_EQ(outcomes(20, 20), (std::array<int, 3>{80, 0, 20}));
	EXPECT_EQ(outcomes(40, 40), (std::array<int, 3>{60, 0, 40}));
	EXPECT_EQ(outcomes(55, 20), (std::array<int, 3>{45, 35, 20}));
	EXPECT_EQ(outcomes(55, 40), (std::array<int, 3>{45, 15, 40}));
}
TEST(RunicReflectionPolicyTest, EffectiveResistanceCapsTheNativeReflectionBand)
{
	EXPECT_EQ(outcomes(10, 40), (std::array<int, 3>{90, 0, 10}));
	EXPECT_EQ(outcomes(0, 40), (std::array<int, 3>{100, 0, 0}));
	EXPECT_EQ(outcomes(-5, 40), (std::array<int, 3>{100, 0, 0}));
	EXPECT_EQ(outcomes(150, 20), (std::array<int, 3>{0, 100, 0}));
	EXPECT_EQ(outcomes(100, 40), (std::array<int, 3>{0, 100, 0}));
	EXPECT_EQ(runic::reflectionChance(100, 40), 0);
	EXPECT_EQ(outcomes(30, 0), (std::array<int, 3>{70, 30, 0}));
}
TEST(RunicReflectionPolicyTest, IllegalOrAlreadyReflectedShareRetainsTheSameResistanceOutcome)
{
	EXPECT_EQ(outcomes(55, 40, false), (std::array<int, 3>{45, 55, 0}));
	EXPECT_EQ(runic::outcome(55, 40, 39, true), runic::Outcome::Reflect);
	EXPECT_EQ(runic::outcome(55, 40, 40, true), runic::Outcome::Resist);
	EXPECT_EQ(runic::outcome(55, 40, 54, true), runic::Outcome::Resist);
	EXPECT_EQ(runic::outcome(55, 40, 55, true), runic::Outcome::Apply);
}
TEST(RunicReflectionPolicyTest, RejectsSamplesOutsideTheExistingResistanceDie)
{
	EXPECT_THROW(runic::outcome(20, 20, -1, true), std::invalid_argument);
	EXPECT_THROW(runic::outcome(20, 20, 100, true), std::invalid_argument);
}

TEST(RunicReflectionPolicyTest, NativeLifeDrainRequiresItsDistinctPairContractWhileUnknownDamageStaysUnattested)
{
	spells::effects::native::Damage ordinary;
	spells::effects::native::LifeDrain paired;
	struct ForeignDamage final : spells::effects::native::Damage {} foreign;
	EXPECT_EQ(ordinary.resistanceShareKind(), spells::effects::ResistanceShareKind::Damage);
	EXPECT_EQ(paired.resistanceShareKind(), spells::effects::ResistanceShareKind::LifeDrain);
	EXPECT_EQ(foreign.resistanceShareKind(), spells::effects::ResistanceShareKind::Unsupported);
}
