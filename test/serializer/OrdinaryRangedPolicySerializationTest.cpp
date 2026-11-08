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
TEST(OrdinaryRangedAdmission, IndependentHistoricalLeafCapabilityFlagRemainsVersioned)
{
	for(int version = 1145; version <= 1147; ++version)
		EXPECT_FALSE(context(static_cast<Version>(version)).ordinaryRangedSpecialties);
	EXPECT_TRUE(context(Version::NEW_HORIZONS_ORDINARY_RANGED_SPECIALTIES).ordinaryRangedSpecialties);
	EXPECT_TRUE(context(Version::CURRENT).ordinaryRangedSpecialties);
}
TEST(OrdinaryRangedAdmission, RetiredRecipesAreRejectedRatherThanReinterpreted)
{
	for(const char * key : {"closeQuartersDrill", "rangefinderFullDamageDistance", "cruelVolleyDamagePercent", "measuredVolleyMdrPenetrationPercent"})
	{
		auto rules = currentOrdinaryOwnerTest::leafRules();
		rules["additionalSpecialties"]["ordinaryCreatureSpecialties"][key].Integer() = 1;
		EXPECT_THROW(newHorizonsHeroes::validateCurrentSpecialtyRules(rules), std::runtime_error);
	}
}
TEST(OrdinaryRangedAdmission, FixedCurrentControllerRecipesRequire1157AndRejectMalformedScalars)
{
	for(const auto & [key, value] : {std::pair{"relentlessBarrageDamagePercent", 50}, std::pair{"pointBlankShotDamageBonusPercent", 100}})
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

constexpr CurrentOrdinaryPolicyCase currentCases[] = {
	{"core:christian", "pointBlankShotDamageBonusPercent", "Point-Blank Shot", 100},
	{"core:jabarkas", "furyOfTheHordeAttack", "Fury of the Horde", 2},
	{"core:theodorus", "dispellingShotMageChancePercent", "Nullifying Shot", 20},
};
class OrdinaryRangedPolicySerializationTest : public CurrentOrdinaryOwnerFixture, public ::testing::WithParamInterface<int> {};
TEST_P(OrdinaryRangedPolicySerializationTest, Current1157RoundtripsHeroPoolMapSettingsWorldAndLobby)
{
	ASSERT_NO_FATAL_FAILURE(prepareCurrent(currentCases[GetParam()]));
	ASSERT_NO_FATAL_FAILURE(currentRoundtrips());
}
TEST_P(OrdinaryRangedPolicySerializationTest, CompleteCurrentOwnersAndFileAdmissionRefuseEarlierFormats)
{
	ASSERT_NO_FATAL_FAILURE(prepareCurrent(currentCases[GetParam()]));
	ASSERT_NO_FATAL_FAILURE(currentOwnersRefuseOlderFormats());
}
TEST_P(OrdinaryRangedPolicySerializationTest, MalformedCurrentHeroAndWorldReadersRejectWithoutChangingTheirSource)
{
	ASSERT_NO_FATAL_FAILURE(prepareCurrent(currentCases[GetParam()]));
	ASSERT_NO_FATAL_FAILURE(malformedCurrentOwnersRejectWithoutChangingTheSource());
}
TEST_P(OrdinaryRangedPolicySerializationTest, CurrentAuthoredOmissionDoesNotInventAnOrdinaryRecipe)
{
	ASSERT_NO_FATAL_FAILURE(prepareCurrent(currentCases[GetParam()], true));
	ASSERT_NO_FATAL_FAILURE(currentAuthoredOmissionSurvivesRoundtrip());
}
INSTANTIATE_TEST_SUITE_P(CurrentNativeProfiles, OrdinaryRangedPolicySerializationTest, ::testing::Range(0, 3));
}
