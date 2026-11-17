/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsPerkRules.h"
#include "../../lib/json/JsonNode.h"

namespace
{
JsonNode prayerRules()
{
	JsonNode rules;
	auto & policy = rules["divineMandatePolicy"];
	policy["version"].Integer() = 3;
	policy["potencyFaithBonusByRank"].Vector() = {JsonNode(0), JsonNode(0), JsonNode(50), JsonNode(100)};
	policy["ferventInvocationAdditionalFaith"].Integer() = 100;
	return rules;
}
}

TEST(ConsecratedArmsPolicy, CompleteCapturedFlatRankProfileIsAccepted)
{
	const auto rules = prayerRules();
	EXPECT_NO_THROW(newHorizonsHeroes::validatePassiveFactionPolicies(rules));
}

TEST(ConsecratedArmsPolicy, PartialOrExtendedProfilesAreRejected)
{
	const auto valid = prayerRules();
	for(const auto * missing : {"potencyFaithBonusByRank", "ferventInvocationAdditionalFaith"})
	{
		auto rules = valid;
		rules["divineMandatePolicy"].Struct().erase(missing);
		EXPECT_THROW(newHorizonsHeroes::validatePassiveFactionPolicies(rules), std::runtime_error);
	}
	auto rules = valid;
	rules["divineMandatePolicy"]["extraActions"].Integer() = 1;
	EXPECT_THROW(newHorizonsHeroes::validatePassiveFactionPolicies(rules), std::runtime_error);
}

TEST(ConsecratedArmsPolicy, UnsupportedValuesAndNumericRepresentationsAreRejected)
{
	for(const auto index : {0u, 1u, 2u, 3u})
	{
		auto rules = prayerRules();
		auto & value = rules["divineMandatePolicy"]["potencyFaithBonusByRank"].Vector()[index];
		value.Integer() += 1;
		EXPECT_THROW(newHorizonsHeroes::validatePassiveFactionPolicies(rules), std::runtime_error);
		value = JsonNode();
		value.Float() = index < 2 ? 0.0 : index == 2 ? 50.0 : 100.0;
		EXPECT_THROW(newHorizonsHeroes::validatePassiveFactionPolicies(rules), std::runtime_error);
	}
	for(const int value : {0, 99, 101})
	{
		auto rules = prayerRules();
		rules["divineMandatePolicy"]["ferventInvocationAdditionalFaith"].Integer() = value;
		EXPECT_THROW(newHorizonsHeroes::validatePassiveFactionPolicies(rules), std::runtime_error);
	}
}
