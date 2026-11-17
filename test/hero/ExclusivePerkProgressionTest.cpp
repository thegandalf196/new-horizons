/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../PerkStateFixture.h"
#include "../../lib/entities/hero/NewHorizonsFourOfferProgression.h"
#include "../../lib/entities/hero/NewHorizonsPerkOperation.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
using namespace newHorizonsHeroes;
constexpr const char * SKILL = "new-horizons:offense";

PerkState state()
{
	PerkState result;
	auto rules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	rules.Struct().erase("unifiedSkillProgressionVersion");
	rules["fourOfferProgressionVersion"].Integer() = 1;
	rules["perkProgressionPolicyVersion"].Integer() = 2;
	result.replaceRules(std::move(rules));
	return result;
}

std::vector<PerkOfferCandidate> pool(const PerkState & state, int rank)
{
	return PerkSelectionOperation(state).eligibleCandidates(SKILL, rank);
}

void selectFirst(PerkState & value, int rank)
{
	const auto choices = pool(value, rank);
	ASSERT_FALSE(choices.empty());
	value.select(SKILL, choices.front().selection.perkId, rank);
}
}

TEST(ExclusivePerkProgression, BasicCompletedTierHasNoPerkGroupAndNextRankOffersAllAdvanced)
{
	auto value = state();
	ASSERT_EQ(pool(value, 1).size(), 4);
	ASSERT_NO_FATAL_FAILURE(selectFirst(value, 1));
	EXPECT_TRUE(pool(value, 1).empty());
	EXPECT_TRUE(value.candidatesForSkillTier(SKILL, 1).empty());
	EXPECT_TRUE(value.canAdvanceSkillNormally(SKILL, 1));
	const auto advanced = pool(value, 2);
	ASSERT_EQ(advanced.size(), 4);
	for(const auto & choice : advanced) EXPECT_EQ(choice.requiredRank, 2);
	const std::vector<FourOfferSkillCandidate> candidates{{SecondarySkill(0), 1, 8, true, pool(value, 1)}};
	const auto offer = prepareFourOfferProgression(candidates, [](const auto &) { return size_t{0}; });
	EXPECT_TRUE(offer.perkSkills[0].empty());
	EXPECT_TRUE(offer.perkSkills[1].empty());
}

TEST(ExclusivePerkProgression, ExternalExpertRankAcquiresExactlyOnePerTierInOrder)
{
	auto value = state();
	for(int tier = 1; tier <= 3; ++tier)
	{
		const auto choices = pool(value, 3);
		ASSERT_EQ(choices.size(), tier == 3 ? 2 : 4);
		for(const auto & choice : choices) EXPECT_EQ(choice.requiredRank, tier);
		ASSERT_NO_FATAL_FAILURE(selectFirst(value, 3));
	}
	EXPECT_EQ(value.getSelected().size(), 3);
	EXPECT_TRUE(pool(value, 3).empty());
	EXPECT_NO_THROW(value.validate());
}

TEST(ExclusivePerkProgression, DuplicateTierAndSkippedPrerequisiteRejectWithoutPublishingChanges)
{
	auto value = state();
	const auto basic = pool(value, 1);
	ASSERT_GE(basic.size(), 2);
	const auto advancedDefinition = perkOptions(value.getRules(), SKILL)[4];
	EXPECT_FALSE(value.canAcquirePerk(SKILL, advancedDefinition.id, 3));
	EXPECT_THROW(value.select(SKILL, advancedDefinition.id, 3), std::runtime_error);
	EXPECT_TRUE(value.getSelected().empty());
	value.select(SKILL, basic.front().selection.perkId, 1);
	const auto before = value.toJson();
	EXPECT_FALSE(value.canAcquirePerk(SKILL, basic.back().selection.perkId, 3));
	EXPECT_THROW(value.select(SKILL, basic.back().selection.perkId, 3), std::runtime_error);
	EXPECT_EQ(value.toJson(), before);
}

TEST(ExclusivePerkProgression, BasicAndAdvancedPerksStillGateNormalRankUp)
{
	auto value = state();
	EXPECT_FALSE(value.canAdvanceSkillNormally(SKILL, 1));
	EXPECT_FALSE(value.canAdvanceSkillNormally(SKILL, 2));
	ASSERT_NO_FATAL_FAILURE(selectFirst(value, 1));
	EXPECT_TRUE(value.canAdvanceSkillNormally(SKILL, 1));
	EXPECT_FALSE(value.canAdvanceSkillNormally(SKILL, 2));
	ASSERT_NO_FATAL_FAILURE(selectFirst(value, 2));
	EXPECT_TRUE(value.canAdvanceSkillNormally(SKILL, 2));
	EXPECT_FALSE(value.canAdvanceSkillNormally(SKILL, 3));
}

TEST(ExclusivePerkProgression, DevelopedHistoricalFourOfferChoicesKeepCapturedPolicyAndBytes)
{
	auto historical = state();
	perkStateFixture::editRules(historical, [&](JsonNode & rules) { rules["perkProgressionPolicyVersion"].Integer() = 1; });
	for(int count = 0; count < 3; ++count) ASSERT_NO_FATAL_FAILURE(selectFirst(historical, 1));
	EXPECT_TRUE(usesStrictPerkProgression(historical.getRules()));
	EXPECT_FALSE(usesExclusivePerkTiers(historical.getRules()));
	CMemorySerializer memory;
	memory.oser & historical;
	PerkState restored;
	memory.iser & restored;
	EXPECT_EQ(restored.getRules(), historical.getRules());
	EXPECT_EQ(restored.getSelected(), historical.getSelected());
	EXPECT_NO_THROW(restored.validate());
	CMemorySerializer reencoded;
	reencoded.oser & restored;
	EXPECT_EQ(reencoded.extractBuffer(), memory.extractBuffer());
}

TEST(ExclusivePerkProgression, MetamagicV4RequiresExactCompleteCapturedProfile)
{
	const auto current = state();
	const auto profile = current.getRules()["metamagicPolicy"];
	ASSERT_EQ(profile["version"].Integer(), 4);
	EXPECT_NO_THROW(validatePassiveFactionPolicies(current.getRules()));
	for(const auto * key : {"removalChancePercent", "damagePerMana", "restorationPerMana", "halonRemovalChanceBonusPercent"})
	{
		auto missing = current.getRules();
		missing["metamagicPolicy"].Struct().erase(key);
		EXPECT_THROW(validatePassiveFactionPolicies(missing), std::runtime_error);
	}
	for(const auto * key : {"damagePerMana", "restorationPerMana", "halonRemovalChanceBonusPercent"})
	{
		auto wrong = current.getRules();
		++wrong["metamagicPolicy"][key].Integer();
		EXPECT_THROW(validatePassiveFactionPolicies(wrong), std::runtime_error);
	}
	auto wrong = current.getRules();
	wrong["metamagicPolicy"]["removalChancePercent"].Vector()[0].Integer() = 20;
	EXPECT_THROW(validatePassiveFactionPolicies(wrong), std::runtime_error);
	wrong = current.getRules();
	wrong["metamagicPolicy"]["adeptAdditionalPoints"].Integer() = 1;
	EXPECT_THROW(validatePassiveFactionPolicies(wrong), std::runtime_error);
}
