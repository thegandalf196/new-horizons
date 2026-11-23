/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"
#include "../PerkStateFixture.h"
#include "../../lib/entities/hero/NewHorizonsFourOfferProgression.h"
#include "../../lib/entities/hero/NewHorizonsPerkOperation.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../server/LegacyEstatesCapture.h"
#include <numeric>

namespace
{
using namespace newHorizonsHeroes;
FourOfferSkillCandidate skill(int id, int rank, int weight, bool advance, size_t perkCount = 0)
{
	FourOfferSkillCandidate result{SecondarySkill(id), rank, weight, advance, {}};
	for(size_t index = 0; index < perkCount; ++index)
		result.perks.push_back({{"test:skill" + std::to_string(id), "test:perk" + std::to_string(index)},
			"Perk " + std::to_string(index), "Description " + std::to_string(index), 1});
	return result;
}

size_t first(const std::vector<int> &) { return 0; }

size_t ticket(const std::vector<int> & weights, int value)
{
	for(size_t index = 0; index < weights.size(); ++index)
	{
		if(value < weights[index]) return index;
		value -= weights[index];
	}
	throw std::runtime_error("Ticket outside the test weight domain");
}

PerkState perkState(bool historicalUnified = false)
{
	PerkState result;
	auto rules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	rules.Struct().erase("unifiedSkillProgressionVersion");
	rules.Struct().erase("fourOfferProgressionVersion");
	// These cases characterize the released policy-1 Four Offer choices.
	rules["perkProgressionPolicyVersion"].Integer() = 1;
	if(historicalUnified) rules["unifiedSkillProgressionVersion"].Integer() = 1;
	else rules["fourOfferProgressionVersion"].Integer() = 1;
	result.replaceRules(std::move(rules));
	return result;
}

std::vector<PerkOfferCandidate> eligible(const PerkState & state, int rank)
{
	return PerkSelectionOperation(state).eligibleCandidates("new-horizons:offense", rank);
}
}

TEST(FourOfferProgression, AdvancedUpgradesTakePriorityAndRetainEightToFourWeights)
{
	const std::vector candidates{skill(0, 2, 8, true), skill(1, 2, 4, true), skill(2, 1, 9, true)};
	std::array<int, 3> selected{};
	for(int value = 0; value < 12; ++value)
	{
		int draws = 0;
		const auto offers = prepareFourOfferProgression(candidates, [&](const auto & weights)
		{
			++draws;
			EXPECT_EQ(weights, (std::vector<int>{8, 4}));
			return ticket(weights, value);
		});
		EXPECT_EQ(draws, 1);
		++selected[offers.skills[0].getNum()];
		EXPECT_EQ(offers.skills[1], SecondarySkill::NONE);
	}
	EXPECT_EQ(selected, (std::array<int, 3>{8, 4, 0}));
}

TEST(FourOfferProgression, NewSkillPoolHasNoDomainPriorityOrDuplicateFallback)
{
	const std::vector candidates{skill(0, 0, 8, true), skill(1, 0, 4, true), skill(2, 0, 1, true)};
	std::array<int, 3> selected{};
	for(int value = 0; value < 13; ++value)
	{
		const auto offers = prepareFourOfferProgression(candidates, [&](const auto & weights)
		{
			EXPECT_EQ(weights, (std::vector<int>{8, 4, 1}));
			return ticket(weights, value);
		});
		EXPECT_EQ(offers.skills[0], SecondarySkill::NONE);
		++selected[offers.skills[1].getNum()];
	}
	EXPECT_EQ(selected, (std::array<int, 3>{8, 4, 1}));
}

TEST(FourOfferProgression, PrimaryPerkUsesActualRankAndAlternativeIgnoresRank)
{
	const std::vector candidates{skill(0, 3, 8, false, 4), skill(1, 3, 4, false, 3), skill(2, 1, 9, false, 4)};
	int draws = 0;
	const auto offers = prepareFourOfferProgression(candidates, [&](const auto & weights)
	{
		EXPECT_EQ(weights, draws++ == 0 ? (std::vector<int>{8, 4}) : (std::vector<int>{4, 9}));
		return draws == 1 ? 0 : 1;
	});
	EXPECT_EQ(draws, 2);
	const auto groups = fourOfferPerkGroups(offers, candidates);
	EXPECT_EQ(groups[0], candidates[0].perks);
	EXPECT_EQ(groups[1], candidates[2].perks);
	EXPECT_EQ(groups[0].front().requiredRank, 1); // Expert missing its earlier perk stage.
	EXPECT_NO_THROW(validateFourOfferProgression(offers, candidates));
}

TEST(FourOfferProgression, LearnedZeroWeightUsesOneOnlyForPerks)
{
	const std::vector candidates{skill(0, 1, 0, true, 4), skill(1, 0, 0, true)};
	int draws = 0;
	const auto offers = prepareFourOfferProgression(candidates, [&](const auto & weights)
	{
		++draws;
		EXPECT_EQ(weights, (std::vector<int>{1}));
		return 0;
	});
	EXPECT_EQ(draws, 1);
	EXPECT_EQ(offers.skills, (std::array<SecondarySkill, 2>{SecondarySkill::NONE, SecondarySkill::NONE}));
	EXPECT_EQ(fourOfferPerkGroups(offers, candidates)[0], candidates[0].perks);
	EXPECT_TRUE(offers.perkSkills[1].empty());
	EXPECT_EQ(candidates[0].weight, 0);
}

TEST(FourOfferProgression, HeldGroupIdentityExposesFullPoolAndRejectsForgedOrDuplicateParents)
{
	const std::vector candidates{skill(0, 1, 8, true, 4), skill(1, 1, 4, true, 4)};
	const auto offers = prepareFourOfferProgression(candidates, first);
	for(int inspection = 0; inspection < 10; ++inspection)
	{
		EXPECT_NO_THROW(validateFourOfferProgression(offers, candidates));
		EXPECT_EQ(fourOfferPerkGroups(offers, candidates)[0], candidates[0].perks);
	}
	auto duplicate = offers;
	duplicate.perkSkills[1] = duplicate.perkSkills[0];
	EXPECT_THROW(validateFourOfferProgression(duplicate, candidates), std::runtime_error);
	auto forged = offers;
	forged.perkSkills[0] = "test:unknown";
	EXPECT_THROW(validateFourOfferProgression(forged, candidates), std::runtime_error);
}

TEST(FourOfferProgression, RepliesPreserveFixedPositionsAndCapturedCatalogueIndices)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const auto state = perkState();
	FourOfferProgressionOffer offers;
	offers.skills[1] = SecondarySkill(3);
	offers.targetRanks[1] = 1;
	offers.perkSkills = {"new-horizons:offense", "new-horizons:armorer"};
	EXPECT_FALSE(fourOfferSelection(offers, state, 0));
	ASSERT_TRUE(fourOfferSelection(offers, state, 1));
	EXPECT_EQ(fourOfferSelection(offers, state, 1)->skill, SecondarySkill(3));
	for(size_t group = 0; group < 2; ++group)
		for(size_t index = 0; index < 10; ++index)
		{
			const auto & row = state.getRules()["skills"][offers.perkSkills[group]]["perks"].Vector()[index];
			const auto reply = fourOfferPerkReply(offers, state, group, row["id"].String());
			EXPECT_EQ(reply, 2 + group * 10 + index);
			const auto selected = fourOfferSelection(offers, state, reply);
			ASSERT_TRUE(selected);
			ASSERT_TRUE(selected->perk);
			EXPECT_EQ(selected->perk->selection.perkId, row["id"].String());
		}
	EXPECT_FALSE(fourOfferSelection(offers, state, -1));
	EXPECT_FALSE(fourOfferSelection(offers, state, 22));
}

TEST(FourOfferProgression, ExhaustedDevelopmentHasOneSafeCompletionReply)
{
	int draws = 0;
	const auto offers = prepareFourOfferProgression({}, [&](const auto &) { ++draws; return 0; });
	EXPECT_TRUE(offers.empty());
	EXPECT_EQ(draws, 0);
	ASSERT_TRUE(fourOfferSelection(offers, PerkState{}, 0));
	EXPECT_TRUE(fourOfferSelection(offers, PerkState{}, 0)->exhausted);
	EXPECT_FALSE(fourOfferSelection(offers, PerkState{}, 1));
}

TEST(FourOfferProgression, ConnoisseurKeepsSlotCategoryAndHighestUpgradeRank)
{
	const std::vector candidates{skill(0, 2, 8, true), skill(1, 2, 4, true), skill(2, 1, 9, true),
		skill(3, 0, 1, true), skill(4, 0, 2, true)};
	const auto offers = prepareFourOfferProgression(candidates, first);
	const auto left = fourOfferRerollCandidates(offers, candidates, 0);
	const auto right = fourOfferRerollCandidates(offers, candidates, 1);
	ASSERT_EQ(left.size(), 1);
	EXPECT_EQ(left.front().skill, SecondarySkill(1));
	ASSERT_EQ(right.size(), 1);
	EXPECT_EQ(right.front().skill, SecondarySkill(4));
	EXPECT_TRUE(fourOfferRerollCandidates(offers, candidates, 2).empty());
}

TEST(FourOfferProgression, CompleteSnapshotRoundTripsWithoutRedrawing)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	HeroLevelUp original;
	original.fourOffers = prepareFourOfferProgression({skill(0, 2, 8, true, 4), skill(1, 0, 1, true)}, first);
	original.restoredOffer = true;
	original.reachedLevel = 5;
	CMemorySerializer memory;
	memory.oser & original;
	HeroLevelUp decoded;
	memory.iser & decoded;
	EXPECT_EQ(decoded.fourOffers, original.fourOffers);
	EXPECT_EQ(decoded.reachedLevel, original.reachedLevel);
	EXPECT_TRUE(decoded.restoredOffer);
}

TEST(FourOfferProgression, NewPayloadRejectsOlderWriterBeforeAnyPrefix)
{
	HeroLevelUp original;
	original.fourOffers.skills[0] = SecondarySkill(0);
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::NEW_HORIZONS_LEADERSHIP_EFFICIENCY;
	EXPECT_THROW(original.serialize(memory.oser), std::runtime_error);
	EXPECT_TRUE(memory.extractBuffer().empty());
}

TEST(FourOfferProgression, ExternalExpertRankStillBeginsWithBasicPerks)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto state = perkState();
	auto offers = eligible(state, 3);
	ASSERT_EQ(offers.size(), 4);
	EXPECT_TRUE(std::all_of(offers.begin(), offers.end(), [](const auto & candidate) { return candidate.requiredRank == 1; }));
	state.select(offers.front().selection.skillId, offers.front().selection.perkId, 3);
	offers = eligible(state, 3);
	EXPECT_EQ(std::count_if(offers.begin(), offers.end(), [](const auto & candidate) { return candidate.requiredRank == 1; }), 3);
	EXPECT_EQ(std::count_if(offers.begin(), offers.end(), [](const auto & candidate) { return candidate.requiredRank == 2; }), 4);
	EXPECT_EQ(std::count_if(offers.begin(), offers.end(), [](const auto & candidate) { return candidate.requiredRank == 3; }), 0);
	const auto advanced = std::find_if(offers.begin(), offers.end(), [](const auto & candidate) { return candidate.requiredRank == 2; });
	ASSERT_NE(advanced, offers.end());
	state.select(advanced->selection.skillId, advanced->selection.perkId, 3);
	const auto expertOffers = eligible(state, 3);
	EXPECT_EQ(std::count_if(expertOffers.begin(), expertOffers.end(), [](const auto & candidate) { return candidate.requiredRank == 3; }), 2);
}

TEST(FourOfferProgression, RequiredPerkStagesGateNormalRankAdvancement)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto state = perkState();
	EXPECT_FALSE(state.canAdvanceSkillNormally("new-horizons:offense", 1));
	EXPECT_FALSE(state.canAdvanceSkillNormally("new-horizons:offense", 2));
	const auto basic = eligible(state, 1).front();
	state.select(basic.selection.skillId, basic.selection.perkId, 1);
	EXPECT_TRUE(state.canAdvanceSkillNormally("new-horizons:offense", 1));
	EXPECT_FALSE(state.canAdvanceSkillNormally("new-horizons:offense", 2));
	const auto pool = eligible(state, 2);
	const auto advanced = std::find_if(pool.begin(), pool.end(), [](const auto & candidate) { return candidate.requiredRank == 2; });
	ASSERT_NE(advanced, pool.end());
	state.select(advanced->selection.skillId, advanced->selection.perkId, 2);
	EXPECT_TRUE(state.canAdvanceSkillNormally("new-horizons:offense", 2));
	EXPECT_FALSE(state.canAdvanceSkillNormally("new-horizons:offense", 3));
}

TEST(FourOfferProgression, ExtraEarlierTierPerkIsLegalAndWarnsWithoutMutatingTheHero)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto state = perkState();
	const auto first = eligible(state, 1).front();
	EXPECT_FALSE(perkChoiceBlocksTierCompletion(state, first, 1));
	state.select(first.selection.skillId, first.selection.perkId, 1);
	const auto second = eligible(state, 1).front();
	const auto before = state.getSelected();
	EXPECT_TRUE(state.canAcquirePerk(second.selection.skillId, second.selection.perkId, 1));
	EXPECT_TRUE(perkChoiceBlocksTierCompletion(state, second, 1));
	EXPECT_EQ(state.getSelected(), before);
	state.select(second.selection.skillId, second.selection.perkId, 1);
	const auto third = eligible(state, 1).front();
	state.select(third.selection.skillId, third.selection.perkId, 1);
	EXPECT_TRUE(eligible(state, 3).empty());
	EXPECT_EQ(state.getSelected().size(), 3);
	EXPECT_FALSE(state.canAdvanceSkillNormally("new-horizons:offense", 2));
}

TEST(FourOfferProgression, HistoricalUnifiedPolicyRetainsItsOnePerTierAndMandatorySemantics)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto state = perkState(true);
	const auto basic = eligible(state, 1).front();
	state.select(basic.selection.skillId, basic.selection.perkId, 1);
	EXPECT_TRUE(eligible(state, 1).empty());
	const auto missing = state.nextRequiredPerkOffer([](const auto & skill)
		{ return skill == "new-horizons:offense" ? 2 : 0; });
	ASSERT_EQ(missing.size(), 4);
	EXPECT_EQ(missing.front().requiredRank, 2);
	EXPECT_FALSE(usesFourOfferProgression(state.getRules()));
}

TEST(FourOfferProgression, CapturedPolicyRejectsOlderStateWriterBeforeItsPrefix)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto state = perkState();
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::NEW_HORIZONS_LEADERSHIP_EFFICIENCY;
	EXPECT_THROW(state.serialize(memory.oser), std::runtime_error);
	EXPECT_TRUE(memory.extractBuffer().empty());
	EXPECT_TRUE(state.nextRequiredPerkOffer([](const auto &) { return 3; }).empty());
}

TEST(FourOfferProgression, ProductionWriterRoundTripsNewPolicyAndMultipleEarlierTierChoices)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto state = perkState();
	for(int count = 0; count < 2; ++count)
	{
		const auto candidate = eligible(state, 1).front();
		state.select(candidate.selection.skillId, candidate.selection.perkId, 1);
	}
	CMemorySerializer memory;
	memory.oser & state;
	PerkState restored;
	memory.iser & restored;
	EXPECT_EQ(restored.getRules(), state.getRules());
	EXPECT_EQ(restored.getSelected(), state.getSelected());
	EXPECT_TRUE(usesFourOfferProgression(restored.getRules()));
}

TEST(FourOfferProgression, Released1159DevelopedStateRemainsReadableWithoutChangingItsPolicy)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	PerkState state;
	state.replaceRules(estatesTest::legacyEstatesPerks());
	for(int tier = 1; tier <= 3; ++tier)
	{
		const auto candidates = state.candidatesForSkillTier("new-horizons:offense", tier);
		ASSERT_FALSE(candidates.empty());
		state.select(candidates.front().selection.skillId, candidates.front().selection.perkId, 3);
	}
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::NEW_HORIZONS_LEADERSHIP_EFFICIENCY;
	memory.iser.version = memory.oser.version;
	memory.oser & state;
	PerkState restored;
	memory.iser & restored;
	EXPECT_EQ(restored.getRules(), state.getRules());
	EXPECT_EQ(restored.getSelected(), state.getSelected());
	EXPECT_TRUE(usesUnifiedSkillProgression(restored.getRules()));
	EXPECT_FALSE(usesFourOfferProgression(restored.getRules()));
}
