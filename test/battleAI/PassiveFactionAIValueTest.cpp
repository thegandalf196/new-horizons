/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "../StdInc.h"
#include "../../AI/BattleAI/PassiveDivineMandateValue.h"
#include "../../AI/BattleAI/TemporaryGatingAI.h"
#include <limits>
using namespace newHorizonsPassiveDivineMandate;

TEST(PassiveFactionAIValue, HierarchicalDrawDoesNotFlattenUnequalTargetDomains)
{
	std::vector<SpellChoice> pool{
		{SpellID(1), SpellID(1), false, {{10, {10}}}},
		{SpellID(2), SpellID(2), false, {{20, {20}}, {21, {21}}, {22, {22}}}}};
	passiveDivineMandateAI::Workload work;
	const auto value = passiveDivineMandateAI::expectedOneDraw(pool,
		[](const SpellChoice &, const TargetChoice & target) -> std::optional<float>
		{ return target.aimUnitId == 10 ? 12.0f : 0.0f; }, &work);
	ASSERT_TRUE(value);
	EXPECT_FLOAT_EQ(*value, 6.0f);
	EXPECT_EQ(work.spells, 2);
	EXPECT_EQ(work.evaluatedOutcomes, 4);
}
TEST(PassiveFactionAIValue, MissingOutcomeRetainsUncertaintyAndProbabilityMass)
{
	std::vector<SpellChoice> pool{{SpellID(1), SpellID(1), false, {{10, {10}}, {11, {11}}}}};
	const auto value = passiveDivineMandateAI::expectedOneDraw(pool,
		[](const SpellChoice &, const TargetChoice & target) -> std::optional<float>
		{ if(target.aimUnitId == 11) return std::nullopt; return 12.0f; });
	EXPECT_FALSE(value);
}
TEST(PassiveFactionAIValue, MassOutcomeIsOneAtomicCastAndKeepsSignedValue)
{
	std::vector<SpellChoice> pool{{SpellID(1), SpellID(1), true, {{10, {10, 11, 12}}}}};
	int calls = 0;
	const auto value = passiveDivineMandateAI::expectedOneDraw(pool,
		[&](const SpellChoice &, const TargetChoice & target) -> std::optional<float>
		{ ++calls; EXPECT_EQ(target.recipientUnitIds.size(), 3); return -4.0f; });
	ASSERT_TRUE(value);
	EXPECT_FLOAT_EQ(*value, -4.0f);
	EXPECT_EQ(calls, 1);
}
TEST(PassiveFactionAIValue, EmptyPoolCallsNoProjection)
{
	int calls = 0;
	const auto value = passiveDivineMandateAI::expectedOneDraw({},
		[&](const SpellChoice &, const TargetChoice &) -> std::optional<float>
		{ ++calls; return 1.0f; });
	ASSERT_TRUE(value);
	EXPECT_FLOAT_EQ(*value, 0.0f);
	EXPECT_EQ(calls, 0);
}
TEST(PassiveFactionAIValue, NonfiniteOutcomeDoesNotBecomeLegalZero)
{
	std::vector<SpellChoice> pool{{SpellID(1), SpellID(1), false, {{10, {10}}}}};
	EXPECT_FALSE(passiveDivineMandateAI::expectedOneDraw(pool,
		[](const SpellChoice &, const TargetChoice &) -> std::optional<float>
		{ return std::numeric_limits<float>::quiet_NaN(); }));
}
TEST(PassiveFactionAIValue, GateSelectionPricesMarginalUtilityAndKeepsStableTies)
{
	std::vector<temporaryGatingAI::Candidate> candidates(3);
	for(size_t i = 0; i < candidates.size(); ++i)
	{ candidates[i].plan.sourceUnitId = i; candidates[i].action.stackNumber = i; }
	bool complete = false;
	temporaryGatingAI::Workload work;
	const auto selected = temporaryGatingAI::choose(candidates,
		[](const temporaryGatingAI::Candidate & candidate) -> std::optional<float>
		{ return candidate.plan.sourceUnitId == 0 ? -1.0f : 3.0f; }, complete, &work);
	EXPECT_TRUE(complete);
	ASSERT_TRUE(selected);
	EXPECT_EQ(selected->stackNumber, 1);
	EXPECT_EQ(work.scoredPlans, 3);
}
TEST(PassiveFactionAIValue, MissingGateValueCannotSilentlyChooseKnownSubset)
{
	std::vector<temporaryGatingAI::Candidate> candidates(2);
	candidates[1].plan.sourceUnitId = 1;
	bool complete = true;
	const auto selected = temporaryGatingAI::choose(candidates,
		[](const temporaryGatingAI::Candidate & candidate) -> std::optional<float>
		{ if(candidate.plan.sourceUnitId) return std::nullopt; return 5.0f; }, complete);
	EXPECT_FALSE(complete);
	EXPECT_FALSE(selected);
}
