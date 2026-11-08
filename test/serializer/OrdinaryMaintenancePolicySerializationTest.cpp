/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CurrentOrdinaryOwnerFixture.h"
#include "../../lib/entities/hero/NewHorizonsSpecialtySerialization.h"
namespace
{
using Version = ESerializationVersion;
newHorizonsHeroes::SpecialtySerializationContext context(Version version)
{
	CMemorySerializer wire; wire.oser.version = version;
	return newHorizonsHeroes::SpecialtySerializationContext::fromHandler(wire.oser);
}
TEST(OrdinaryMaintenanceAdmission, IndependentHistoricalLeafCapabilityFlagRemainsVersioned)
{
	for(int version = 1145; version <= 1148; ++version)
		EXPECT_FALSE(context(static_cast<Version>(version)).ordinaryMaintenanceSpecialties);
	EXPECT_TRUE(context(Version::NEW_HORIZONS_ORDINARY_MAINTENANCE_SPECIALTIES).ordinaryMaintenanceSpecialties);
	EXPECT_TRUE(context(Version::CURRENT).ordinaryMaintenanceSpecialties);
}
TEST(OrdinaryMaintenanceAdmission, RetiredRecipesAreRejectedRatherThanReinterpreted)
{
	for(const char * key : {"fieldReassemblyHealingHP", "fieldMaintenanceHealingHP", "ogreBulwarkRetaliations"})
	{
		auto rules = currentOrdinaryOwnerTest::leafRules();
		rules["additionalSpecialties"]["ordinaryCreatureSpecialties"][key].Integer() = 1;
		EXPECT_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(rules), std::runtime_error);
	}
}
TEST(OrdinaryMaintenanceAdmission, FixedCurrentControllerRecipesRequire1157AndRejectMalformedScalars)
{
	for(const auto & [key, value] : {std::pair{"goldStandardUpgrade", 1}, std::pair{"ogreBloodlustPolicyVersion", 1}})
	{
		auto rules = currentOrdinaryOwnerTest::leafRules();
		auto & scalar = rules["additionalSpecialties"]["ordinaryCreatureSpecialties"][key];
		scalar.Integer() = value;
		ASSERT_NO_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(rules));
		EXPECT_NO_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(Version::CURRENT)));
		EXPECT_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(Version::NEW_HORIZONS_FACTION_SKILL_REVISIONS)), std::runtime_error);
		for(const int invalid : {-1, 0, value + 1})
		{
			scalar.Integer() = invalid;
			EXPECT_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(rules), std::runtime_error);
		}
		scalar.Bool() = false;
		EXPECT_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(rules), std::runtime_error);
	}
}
TEST(OrdinaryMaintenanceAdmission, CrippleMagnitudeAndDurationAreAnAtomicFixedPair)
{
	auto rules = currentOrdinaryOwnerTest::leafRules();
	auto & policy = rules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	policy["cripplingBoltSpeedReduction"].Integer() = 3;
	EXPECT_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(rules), std::runtime_error);
	policy["cripplingBoltDuration"].Integer() = 3;
	EXPECT_NO_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(rules));
	EXPECT_THROW(newHorizonsHeroes::validateCapturedSpecialtiesSerialization(rules, context(Version::NEW_HORIZONS_FACTION_SKILL_REVISIONS)), std::runtime_error);
	policy["cripplingBoltSpeedReduction"].Integer() = 4;
	EXPECT_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(rules), std::runtime_error);
	policy.Struct().erase("cripplingBoltSpeedReduction");
	EXPECT_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(rules), std::runtime_error);
}

constexpr CurrentOrdinaryPolicyCase currentCases[] = {
	{"core:josephine", "goldStandardUpgrade", "Gold Standard", 1},
	{"core:gerwulf", "cripplingBoltSpeedReduction", "Trapping Shot", 3},
	{"core:krellion", "ogreBloodlustPolicyVersion", "Ogre Bulwark", 1},
};
class OrdinaryMaintenancePolicySerializationTest : public CurrentOrdinaryOwnerFixture, public ::testing::WithParamInterface<int> {};
TEST_P(OrdinaryMaintenancePolicySerializationTest, Current1157RoundtripsHeroPoolMapSettingsWorldAndLobby)
{
	ASSERT_NO_FATAL_FAILURE(prepareCurrent(currentCases[GetParam()]));
	ASSERT_NO_FATAL_FAILURE(currentRoundtrips());
}
TEST_P(OrdinaryMaintenancePolicySerializationTest, CompleteCurrentOwnersAndFileAdmissionRefuseEarlierFormats)
{
	ASSERT_NO_FATAL_FAILURE(prepareCurrent(currentCases[GetParam()]));
	ASSERT_NO_FATAL_FAILURE(currentOwnersRefuseOlderFormats());
}
TEST_P(OrdinaryMaintenancePolicySerializationTest, MalformedCurrentHeroAndWorldReadersRejectWithoutChangingTheirSource)
{
	ASSERT_NO_FATAL_FAILURE(prepareCurrent(currentCases[GetParam()]));
	ASSERT_NO_FATAL_FAILURE(malformedCurrentOwnersRejectWithoutChangingTheSource());
}
TEST_P(OrdinaryMaintenancePolicySerializationTest, CurrentAuthoredOmissionDoesNotInventAnOrdinaryRecipe)
{
	ASSERT_NO_FATAL_FAILURE(prepareCurrent(currentCases[GetParam()], true));
	ASSERT_NO_FATAL_FAILURE(currentAuthoredOmissionSurvivesRoundtrip());
}
INSTANTIATE_TEST_SUITE_P(CurrentNativeProfiles, OrdinaryMaintenancePolicySerializationTest, ::testing::Range(0, 3));
}
