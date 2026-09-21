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

const std::string SKILL = "new-horizons:sorceryMagic";
// Keep this fixture pointed at a skill whose entire perk pool is still planned.
// Offense now has the active Shock Assault vertical slice.
const std::string PLANNED_SKILL = "new-horizons:armorer";
}

TEST(NewHorizonsPerkState, LegacyStateRemainsEmptyAndCannotSelect)
{
	newHorizonsHeroes::PerkState legacy;
	EXPECT_NO_THROW(legacy.validate());
	EXPECT_TRUE(legacy.toJson().isNull());
	EXPECT_THROW(legacy.select(SKILL, "new-horizons:offense.shockAssault", 3), std::runtime_error);
}

TEST(NewHorizonsPerkState, SelectionEnforcesExactlyOnePerTierAndThreePerSkillCap)
{
	auto saved = state();
	const auto options = newHorizonsHeroes::perkOptions(saved.rules, SKILL);
	ASSERT_EQ(options.size(), 10u);
	EXPECT_THROW(saved.select(SKILL, options[0].id, -1), std::runtime_error);
	EXPECT_THROW(saved.select(SKILL, options[0].id, 4), std::runtime_error);
	EXPECT_THROW(saved.select(SKILL, options[4].id, 1), std::runtime_error);
	saved.select(SKILL, options[0].id, 3);
	EXPECT_THROW(saved.select(SKILL, options[0].id, 3), std::runtime_error);
	EXPECT_THROW(saved.select(SKILL, options[1].id, 3), std::runtime_error);
	saved.select(SKILL, options[4].id, 3);
	EXPECT_THROW(saved.select(SKILL, options[5].id, 3), std::runtime_error);
	saved.select(SKILL, options[8].id, 3);
	EXPECT_THROW(saved.select(SKILL, options[9].id, 3), std::runtime_error);
	EXPECT_THROW(saved.select(SKILL, "new-horizons:offense.unknown", 3), std::runtime_error);
	EXPECT_EQ(saved.selected.size(), 3u);
}

TEST(NewHorizonsPerkState, OccupiedTierOnlyBlocksAlternativesFromTheSameSkill)
{
	auto saved = state();
	const auto sorceryOptions = newHorizonsHeroes::perkOptions(saved.rules, SKILL);
	const std::string otherSkill = "new-horizons:discipline";
	ASSERT_GE(sorceryOptions.size(), 2u);
	const std::string disciplinePerk = "new-horizons:discipline.inspirationalLeader";

	saved.select(SKILL, sorceryOptions[0].id, 1);
	EXPECT_THROW(saved.select(SKILL, sorceryOptions[1].id, 1), std::runtime_error);
	EXPECT_NO_THROW(saved.select(otherSkill, disciplinePerk, 1));
	EXPECT_TRUE(saved.hasSelection(SKILL, sorceryOptions[0].id));
	EXPECT_TRUE(saved.hasSelection(otherSkill, disciplinePerk));
}

TEST(NewHorizonsPerkState, OfferKeepsSameTierPerksFromOtherSkillsEligible)
{
	auto saved = state();
	const auto sorceryOptions = newHorizonsHeroes::perkOptions(saved.rules, SKILL);
	ASSERT_GE(sorceryOptions.size(), 2u);
	saved.select(SKILL, sorceryOptions[0].id, 1);

	const auto offer = saved.prepareOffer([](const std::string & skillId)
	{
		return skillId == SKILL || skillId == "new-horizons:discipline" ? 1 : 0;
	}, 17);
	ASSERT_EQ(offer.size(), 1u);
	EXPECT_EQ(offer.front().selection.skillId, "new-horizons:discipline");
	EXPECT_EQ(offer.front().selection.perkId, "new-horizons:discipline.inspirationalLeader");
	EXPECT_EQ(offer.front().requiredRank, 1);
}

TEST(NewHorizonsPerkState, PlannedAndRankLockedEffectsNeverProjectAsEnabled)
{
	auto saved = state();
	const auto planned = newHorizonsHeroes::perkOptions(saved.rules, PLANNED_SKILL);
	ASSERT_GE(planned.size(), 2u);
	const auto & perk = planned.front();
	EXPECT_THROW(saved.select(PLANNED_SKILL, planned[1].id, 3), std::runtime_error);
	// Existing saves may contain a planned selection from an earlier registry;
	// keep loading it, but never allow a new one to be authored.
	saved.selected.push_back({PLANNED_SKILL, perk.id});
	saved.validate();
	auto projected = saved.project([](const std::string &) { return 3; });
	ASSERT_EQ(projected.size(), 1u);
	EXPECT_FALSE(projected.front().enabled);
	EXPECT_EQ(projected.front().effect["status"].String(), "planned");
	saved.rules["skills"][PLANNED_SKILL]["perks"].Vector()[0]["effect"]["status"].String() = "active";
	projected = saved.project([](const std::string &) { return 0; });
	EXPECT_FALSE(projected.front().enabled);
	projected = saved.project([](const std::string &) { return 1; });
	EXPECT_TRUE(projected.front().enabled);
}

TEST(NewHorizonsPerkState, OverchargerActivationComesFromTheSavedRegistrySnapshot)
{
	auto active = state();
	constexpr auto SKILL = "new-horizons:sorceryMagic";
	constexpr auto PERK = "new-horizons:sorceryMagic.overcharger";
	for(auto & perk : active.rules["skills"][SKILL]["perks"].Vector())
		if(perk["id"].String() == PERK)
			perk["effect"]["status"].String() = "active";
	active.select(SKILL, PERK, 1);
	const auto activeProjection = active.project([](const std::string &) { return 1; });
	ASSERT_EQ(activeProjection.size(), 1u);
	EXPECT_TRUE(activeProjection.front().enabled);

	auto oldSave = active;
	for(auto & perk : oldSave.rules["skills"][SKILL]["perks"].Vector())
		if(perk["id"].String() == PERK)
			perk["effect"]["status"].String() = "planned";
	const auto oldProjection = oldSave.project([](const std::string &) { return 1; });
	ASSERT_EQ(oldProjection.size(), 1u);
	EXPECT_FALSE(oldProjection.front().enabled);
}

TEST(NewHorizonsPerkState, JsonAndBinaryRoundTripsPreserveSavedRegistrySnapshot)
{
	auto original = state();
	const auto perk = newHorizonsHeroes::perkOptions(original.rules, SKILL).front();
	original.select(SKILL, perk.id, 3);
	const auto legacyPlanned = newHorizonsHeroes::perkOptions(original.rules, PLANNED_SKILL).front();
	original.selected.push_back({PLANNED_SKILL, legacyPlanned.id});
	original.validate();
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
	EXPECT_TRUE(fromJson.hasSelection(PLANNED_SKILL, legacyPlanned.id));
	EXPECT_TRUE(fromBinary.hasSelection(PLANNED_SKILL, legacyPlanned.id));
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

TEST(NewHorizonsPerkState, LegacySameTierSelectionsKeepTheEarliestChoiceOnLoad)
{
	auto original = state();
	const auto options = newHorizonsHeroes::perkOptions(original.rules, SKILL);
	JsonNode legacy;
	legacy["stateVersion"].Integer() = 1;
	legacy["rules"] = original.rules;
	legacy["selected"].Vector();
	for(const auto index : {0, 1, 4, 5, 8})
	{
		JsonNode selection;
		selection["skillId"].String() = SKILL;
		selection["perkId"].String() = options[index].id;
		legacy["selected"].Vector().push_back(std::move(selection));
	}

	const auto restored = newHorizonsHeroes::PerkState::fromJson(legacy);
	ASSERT_EQ(restored.selected.size(), 3u);
	EXPECT_TRUE(restored.hasSelection(SKILL, options[0].id));
	EXPECT_TRUE(restored.hasSelection(SKILL, options[4].id));
	EXPECT_TRUE(restored.hasSelection(SKILL, options[8].id));
	EXPECT_FALSE(restored.hasSelection(SKILL, options[1].id));
	EXPECT_FALSE(restored.hasSelection(SKILL, options[5].id));
}

TEST(NewHorizonsPerkState, OfferIsDeterministicBoundedAndUsesOnlyLearnedEligibleSkills)
{
	auto saved = state();
	const auto ranks = [](const std::string & skillId)
	{
		if(skillId == SKILL)
			return 1;
		return 0;
	};
	const auto first = saved.prepareOffer(ranks, 42);
	const auto repeated = saved.prepareOffer(ranks, 42);
	ASSERT_EQ(first, repeated);
	ASSERT_EQ(first.size(), 2u);
	for(const auto & candidate : first)
	{
		EXPECT_EQ(candidate.selection.skillId, SKILL);
		EXPECT_LE(candidate.requiredRank, ranks(candidate.selection.skillId));
		EXPECT_FALSE(candidate.name.empty());
		EXPECT_FALSE(candidate.description.empty());
		const auto definition = newHorizonsHeroes::perkDefinition(saved.rules,
			candidate.selection.skillId, candidate.selection.perkId);
		ASSERT_TRUE(definition);
		EXPECT_EQ(definition->effect["status"].String(), "active");
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

TEST(NewHorizonsPerkState, OfferExcludesPlannedPerksAndCanBeEmpty)
{
	auto saved = state();
	const auto plannedOnly = saved.prepareOffer([](const std::string & skillId)
	{
		return skillId == PLANNED_SKILL ? 3 : 0;
	}, 9);
	EXPECT_TRUE(plannedOnly.empty());

	const auto mixed = saved.prepareOffer([](const std::string & skillId)
	{
		if(skillId == PLANNED_SKILL)
			return 3;
		if(skillId == SKILL)
			return 1;
		return 0;
	}, 9);
	ASSERT_FALSE(mixed.empty());
	for(const auto & candidate : mixed)
		EXPECT_EQ(candidate.selection.skillId, SKILL);
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

TEST(NewHorizonsPerkState, OfferNeverRepeatsAnOccupiedTierForTheSameSkill)
{
	auto saved = state();
	const auto options = newHorizonsHeroes::perkOptions(saved.rules, SKILL);
	saved.select(SKILL, options[0].id, 3);
	const auto offer = saved.prepareOffer([](const std::string & skillId)
	{
		return skillId == SKILL ? 3 : 0;
	}, 42);
	ASSERT_FALSE(offer.empty());
	for(const auto & candidate : offer)
	{
		EXPECT_EQ(candidate.selection.skillId, SKILL);
		EXPECT_NE(candidate.requiredRank, 1);
	}
}

TEST(NewHorizonsPerkState, LegacyRulesNeverInventAnOffer)
{
	newHorizonsHeroes::PerkState legacy;
	EXPECT_TRUE(legacy.prepareOffer([](const std::string &) { return 3; }, 1).empty());
	EXPECT_THROW(legacy.acceptOffer({}, 0, [](const std::string &) { return 3; }, 1), std::runtime_error);
}
