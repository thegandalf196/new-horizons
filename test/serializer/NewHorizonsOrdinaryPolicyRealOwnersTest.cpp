/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CurrentOrdinaryOwnerFixture.h"
namespace
{

constexpr CurrentOrdinaryPolicyCase currentCases[] = {
	{"core:pasis", "prismaticScreenReductionPercent", "Prismatic Screen", 15},
	{"core:tyraxor", "ridersExploitDamagePercent", "Rider's Exploit", 20},
};
class OrdinaryPolicyRealOwnersTest : public CurrentOrdinaryOwnerFixture, public ::testing::WithParamInterface<int> {};
TEST_P(OrdinaryPolicyRealOwnersTest, Current1157RoundtripsHeroPoolMapSettingsWorldAndLobby)
{
	ASSERT_NO_FATAL_FAILURE(prepareCurrent(currentCases[GetParam()]));
	ASSERT_NO_FATAL_FAILURE(currentRoundtrips());
}
TEST_P(OrdinaryPolicyRealOwnersTest, CompleteCurrentOwnersAndFileAdmissionRefuseEarlierFormats)
{
	ASSERT_NO_FATAL_FAILURE(prepareCurrent(currentCases[GetParam()]));
	ASSERT_NO_FATAL_FAILURE(currentOwnersRefuseOlderFormats());
}
TEST_P(OrdinaryPolicyRealOwnersTest, MalformedCurrentHeroAndWorldReadersRejectWithoutChangingTheirSource)
{
	ASSERT_NO_FATAL_FAILURE(prepareCurrent(currentCases[GetParam()]));
	ASSERT_NO_FATAL_FAILURE(malformedCurrentOwnersRejectWithoutChangingTheSource());
}
TEST_P(OrdinaryPolicyRealOwnersTest, CurrentAuthoredOmissionDoesNotInventAnOrdinaryRecipe)
{
	ASSERT_NO_FATAL_FAILURE(prepareCurrent(currentCases[GetParam()], true));
	ASSERT_NO_FATAL_FAILURE(currentAuthoredOmissionSurvivesRoundtrip());
}
INSTANTIATE_TEST_SUITE_P(CurrentNativeProfiles, OrdinaryPolicyRealOwnersTest, ::testing::Range(0, 2));
}
