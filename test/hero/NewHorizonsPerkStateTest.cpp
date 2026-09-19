/*
 * NewHorizonsPerkStateTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/serializer/CMemorySerializer.h"

#include <stdexcept>

namespace
{
newHorizonsHeroes::PerkState state()
{
	newHorizonsHeroes::PerkState result;
	result.rules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	return result;
}

const std::string SKILL = "new-horizons:offense";
}

TEST(NewHorizonsPerkState, LegacyStateRemainsEmptyAndCannotSelect)
{
	newHorizonsHeroes::PerkState legacy;
	EXPECT_NO_THROW(legacy.validate());
	EXPECT_TRUE(legacy.toJson().isNull());
	EXPECT_THROW(legacy.select(SKILL, "new-horizons:offense.shockAssault", 3), std::runtime_error);
}

TEST(NewHorizonsPerkState, SelectionEnforcesRankDuplicateAndThreePerSkillCap)
{
	auto saved = state();
	const auto options = newHorizonsHeroes::perkOptions(saved.rules, SKILL);
	ASSERT_EQ(options.size(), 10u);
	EXPECT_THROW(saved.select(SKILL, options[0].id, -1), std::runtime_error);
	EXPECT_THROW(saved.select(SKILL, options[0].id, 4), std::runtime_error);
	EXPECT_THROW(saved.select(SKILL, options[4].id, 1), std::runtime_error);
	saved.select(SKILL, options[0].id, 3);
	EXPECT_THROW(saved.select(SKILL, options[0].id, 3), std::runtime_error);
	saved.select(SKILL, options[1].id, 3);
	saved.select(SKILL, options[2].id, 3);
	EXPECT_THROW(saved.select(SKILL, options[3].id, 3), std::runtime_error);
	EXPECT_THROW(saved.select(SKILL, "new-horizons:offense.unknown", 3), std::runtime_error);
	EXPECT_EQ(saved.selected.size(), 3u);
}

TEST(NewHorizonsPerkState, PlannedAndRankLockedEffectsNeverProjectAsEnabled)
{
	auto saved = state();
	const auto perk = newHorizonsHeroes::perkOptions(saved.rules, SKILL).front();
	saved.select(SKILL, perk.id, 3);
	auto projected = saved.project([](const std::string &) { return 3; });
	ASSERT_EQ(projected.size(), 1u);
	EXPECT_FALSE(projected.front().enabled);
	EXPECT_EQ(projected.front().effect["status"].String(), "planned");
	saved.rules["skills"][SKILL]["perks"].Vector()[0]["effect"]["status"].String() = "active";
	projected = saved.project([](const std::string &) { return 0; });
	EXPECT_FALSE(projected.front().enabled);
	projected = saved.project([](const std::string &) { return 1; });
	EXPECT_TRUE(projected.front().enabled);
}

TEST(NewHorizonsPerkState, JsonAndBinaryRoundTripsPreserveSavedRegistrySnapshot)
{
	auto original = state();
	const auto perk = newHorizonsHeroes::perkOptions(original.rules, SKILL).front();
	original.select(SKILL, perk.id, 3);
	const auto savedJson = original.toJson();
	auto fromJson = newHorizonsHeroes::PerkState::fromJson(savedJson);
	ASSERT_EQ(fromJson.selected, original.selected);
	EXPECT_EQ(fromJson.rules, original.rules);

	CMemorySerializer memory;
	memory.oser & original;
	newHorizonsHeroes::PerkState fromBinary;
	memory.iser & fromBinary;
	EXPECT_EQ(fromBinary.selected, original.selected);
	EXPECT_EQ(fromBinary.rules, original.rules);

	original.rules["skills"].Struct().clear();
	EXPECT_TRUE(fromJson.hasSelection(SKILL, perk.id));
	EXPECT_TRUE(fromBinary.hasSelection(SKILL, perk.id));
}

TEST(NewHorizonsPerkState, CrossoverRejectsUnknownFieldsAndForgedSelections)
{
	auto original = state();
	const auto perk = newHorizonsHeroes::perkOptions(original.rules, SKILL).front();
	original.select(SKILL, perk.id, 3);
	auto invalid = original.toJson();
	invalid["unexpected"].Bool() = true;
	EXPECT_THROW(newHorizonsHeroes::PerkState::fromJson(invalid), std::runtime_error);
	invalid = original.toJson();
	invalid["selected"].Vector()[0]["perkId"].String() = "new-horizons:offense.forged";
	EXPECT_THROW(newHorizonsHeroes::PerkState::fromJson(invalid), std::runtime_error);
}

TEST(NewHorizonsPerkState, OfferIsDeterministicBoundedAndUsesOnlyLearnedEligibleSkills)
{
	auto saved = state();
	const auto ranks = [](const std::string & skillId)
	{
		if(skillId == "new-horizons:offense")
			return 1;
		if(skillId == "new-horizons:armorer")
			return 2;
		return 0;
	};
	const auto first = saved.prepareOffer(ranks, 42);
	const auto repeated = saved.prepareOffer(ranks, 42);
	ASSERT_EQ(first, repeated);
	ASSERT_EQ(first.size(), 2u);
	for(const auto & candidate : first)
	{
		EXPECT_TRUE(candidate.selection.skillId == "new-horizons:offense"
			|| candidate.selection.skillId == "new-horizons:armorer");
		EXPECT_LE(candidate.requiredRank, ranks(candidate.selection.skillId));
		EXPECT_FALSE(candidate.name.empty());
		EXPECT_FALSE(candidate.description.empty());
	}
	for(uint64_t seed = 43; seed < 48; ++seed)
	{
		const auto varied = saved.prepareOffer(ranks, seed);
		ASSERT_EQ(varied.size(), 2u);
		for(const auto & candidate : varied)
			EXPECT_LE(candidate.requiredRank, ranks(candidate.selection.skillId));
	}
	EXPECT_TRUE(saved.prepareOffer([](const std::string &) { return 0; }, 42).empty());
}

TEST(NewHorizonsPerkState, AcceptedOfferRevalidatesSnapshotRankDuplicateAndCap)
{
	auto saved = state();
	const auto expert = [](const std::string & skillId)
	{
		return skillId == SKILL ? 3 : 0;
	};
	auto offer = saved.prepareOffer(expert, 9);
	ASSERT_EQ(offer.size(), 2u);
	auto forged = offer;
	forged[0].description += " forged";
	EXPECT_THROW(saved.acceptOffer(forged, 0, expert, 9), std::runtime_error);
	auto duplicate = offer;
	duplicate[1] = duplicate[0];
	EXPECT_THROW(saved.acceptOffer(duplicate, 0, expert, 9), std::runtime_error);
	auto oversized = offer;
	oversized.push_back(offer[0]);
	EXPECT_THROW(saved.acceptOffer(oversized, 0, expert, 9), std::runtime_error);
	EXPECT_THROW(saved.acceptOffer(offer, 2, expert, 9), std::runtime_error);
	EXPECT_THROW(saved.acceptOffer(offer, 0, [](const std::string &) { return 0; }, 9), std::runtime_error);
	uint64_t differentSeed = 10;
	while(differentSeed < 1000 && saved.prepareOffer(expert, differentSeed) == offer)
		++differentSeed;
	ASSERT_LT(differentSeed, 1000u);
	EXPECT_THROW(saved.acceptOffer(offer, 0, expert, differentSeed), std::runtime_error);

	saved.acceptOffer(offer, 0, expert, 9);
	EXPECT_EQ(saved.selected.size(), 1u);
	EXPECT_THROW(saved.acceptOffer(offer, 1, expert, 9), std::runtime_error);
	for(uint64_t seed = 10; saved.selected.size() < 3; ++seed)
	{
		auto next = saved.prepareOffer(expert, seed);
		ASSERT_FALSE(next.empty());
		saved.acceptOffer(next, 0, expert, seed);
	}
	EXPECT_TRUE(saved.prepareOffer(expert, 99).empty());
}

TEST(NewHorizonsPerkState, LegacyRulesNeverInventAnOffer)
{
	newHorizonsHeroes::PerkState legacy;
	EXPECT_TRUE(legacy.prepareOffer([](const std::string &) { return 3; }, 1).empty());
	EXPECT_THROW(legacy.acceptOffer({}, 0, [](const std::string &) { return 3; }, 1), std::runtime_error);
}
