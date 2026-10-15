/*
 * NewHorizonsPerkAvailabilityTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"

#include "../../client/windows/NewHorizonsPerkAvailability.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/modding/ModScope.h"
#include "../../lib/modding/IdentifierStorage.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace
{
constexpr std::string_view SKILL = "new-horizons:discipline";
constexpr std::string_view BASIC = "new-horizons:discipline.inspirationalLeader";
constexpr std::string_view BASIC_ALTERNATIVE = "new-horizons:discipline.steadfast";
constexpr std::string_view BASIC_PLANNED = "new-horizons:discipline.espritDeCorps";
constexpr std::string_view ADVANCED = "new-horizons:discipline.standardBearer";
constexpr std::string_view ADVANCED_ALTERNATIVE = "new-horizons:discipline.fearless";
constexpr std::string_view EXPERT = "new-horizons:discipline.unbreakable";

newHorizonsHeroes::PerkState makeState()
{
	newHorizonsHeroes::PerkState result;
	result.rules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	// Existing cases explicitly exercise captured policy1 semantics.
	result.rules["offerWeightPolicyVersion"].Integer() = 1;
	result.rules.Struct().erase("perkOfferNormalization");
	result.rules.Struct().erase("perkProgressionPolicyVersion");
	return result;
}

newHorizonsHeroes::PerkState makeR2State()
{
	newHorizonsHeroes::PerkState result;
	result.rules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	result.rules.Struct().erase("perkProgressionPolicyVersion"); // Captured historical R2.
	return result;
}

newHorizonsHeroes::PerkDefinition definition(const newHorizonsHeroes::PerkState & state, std::string_view perkId)
{
	const auto perk = newHorizonsHeroes::perkDefinition(state.rules, SKILL, perkId);
	if(!perk)
		throw std::runtime_error("Missing fixture perk definition");
	return *perk;
}

newHorizonsHeroes::PerkState::OfferWeightContext capturedAvailabilityWeights()
{
	const auto classId = LIBRARY->identifiers()->getIdentifier(
		ModScope::scopeGame(), HeroClassID::entityType(), "core:knight", true);
	if(!classId)
		throw std::runtime_error("Missing canonical Knight class for availability fixture");
	const auto captured = newHorizonsHeroes::resolveHeroRules(
		JsonNode(JsonPath::builtin("config/newHorizonsHeroes")), HeroClassID(*classId));
	return {newHorizonsHeroes::PerkState::OfferWeightMode::CapturedTable,
		[captured](const std::string & skillId) -> std::optional<int>
		{
			const auto decoded = LIBRARY->identifiers()->getIdentifier(
				ModScope::scopeGame(), SecondarySkill::entityType(), skillId, true);
			if(!decoded || SecondarySkill::encode(*decoded) != skillId)
				throw std::runtime_error("Noncanonical parent skill in availability fixture");
			return newHorizonsHeroes::skillOfferWeight(captured, SecondarySkill(*decoded));
		}};
}

int rankFor(const std::string & skillId)
{
	return skillId == SKILL ? 3 : 0;
}
}

TEST(NewHorizonsPerkAvailability, MirrorsLearnedRankAndServerOfferEligibility)
{
	auto state = makeState();
	const auto basic = definition(state, BASIC);
	const auto notLearned = newHorizonsPerkAvailability::evaluatePerk(state, SKILL, 0, basic);
	EXPECT_EQ(notLearned.status, newHorizonsPerkAvailability::Status::LOCKED);
	EXPECT_EQ(notLearned.reason, newHorizonsPerkAvailability::Reason::SKILL_NOT_LEARNED);

	const auto available = newHorizonsPerkAvailability::evaluatePerk(state, SKILL, 1, basic);
	EXPECT_EQ(available.status, newHorizonsPerkAvailability::Status::AVAILABLE);
	EXPECT_EQ(available.reason, newHorizonsPerkAvailability::Reason::NONE);

	const auto offer = state.prepareOffer(rankFor, 42, capturedAvailabilityWeights());
	ASSERT_FALSE(offer.empty());
	for(const auto & candidate : offer)
	{
		EXPECT_EQ(candidate.requiredRank, 1);
		const auto candidatePerk = definition(state, candidate.selection.perkId);
		const auto evaluation = newHorizonsPerkAvailability::evaluatePerk(
			state, candidate.selection.skillId, 3, candidatePerk);
		EXPECT_EQ(evaluation.status, newHorizonsPerkAvailability::Status::AVAILABLE);
	}
}

TEST(NewHorizonsPerkAvailability, ExceptionalRankDoesNotSkipEarlierPerkTier)
{
	auto state = makeState();
	const auto advanced = definition(state, ADVANCED);
	const auto rankLocked = newHorizonsPerkAvailability::evaluatePerk(state, SKILL, 1, advanced);
	EXPECT_EQ(rankLocked.status, newHorizonsPerkAvailability::Status::LOCKED);
	EXPECT_EQ(rankLocked.reason, newHorizonsPerkAvailability::Reason::INSUFFICIENT_RANK);

	const auto locked = newHorizonsPerkAvailability::evaluatePerk(state, SKILL, 3, advanced);
	EXPECT_EQ(locked.status, newHorizonsPerkAvailability::Status::LOCKED);
	EXPECT_EQ(locked.reason, newHorizonsPerkAvailability::Reason::EARLIER_TIER_MISSING);

	const auto offer = state.prepareOffer(rankFor, 43, capturedAvailabilityWeights());
	ASSERT_FALSE(offer.empty());
	for(const auto & candidate : offer)
		EXPECT_EQ(candidate.requiredRank, 1);

	state.select(std::string(SKILL), std::string(BASIC), 3);
	const auto nextTier = newHorizonsPerkAvailability::evaluatePerk(state, SKILL, 3, advanced);
	EXPECT_EQ(nextTier.status, newHorizonsPerkAvailability::Status::AVAILABLE);
	const auto advancedOffer = state.prepareOffer(rankFor, 44, capturedAvailabilityWeights());
	ASSERT_FALSE(advancedOffer.empty());
	for(const auto & candidate : advancedOffer)
		EXPECT_EQ(candidate.requiredRank, 2);
}

TEST(NewHorizonsPerkAvailability, DistinguishesOccupiedTiersAndPerSkillCap)
{
	auto state = makeState();
	state.select(std::string(SKILL), std::string(BASIC), 1);
	const auto selected = newHorizonsPerkAvailability::evaluatePerk(
		state, SKILL, 1, definition(state, BASIC));
	EXPECT_EQ(selected.status, newHorizonsPerkAvailability::Status::ACQUIRED);

	const auto occupied = newHorizonsPerkAvailability::evaluatePerk(
		state, SKILL, 1, definition(state, BASIC_ALTERNATIVE));
	EXPECT_EQ(occupied.status, newHorizonsPerkAvailability::Status::LOCKED);
	EXPECT_EQ(occupied.reason, newHorizonsPerkAvailability::Reason::TIER_OCCUPIED);

	state.select(std::string(SKILL), std::string(ADVANCED), 2);
	state.select(std::string(SKILL), std::string(EXPERT), 3);
	const auto capped = newHorizonsPerkAvailability::evaluatePerk(
		state, SKILL, 3, definition(state, ADVANCED_ALTERNATIVE));
	EXPECT_EQ(capped.status, newHorizonsPerkAvailability::Status::LOCKED);
	EXPECT_EQ(capped.reason, newHorizonsPerkAvailability::Reason::PER_SKILL_CAP);
}

TEST(NewHorizonsPerkAvailability, PlannedAndMissingCataloguesFailClosed)
{
	auto state = makeState();
	// Exercise a captured planned entry without depending on which current
	// production effects have since been implemented.
	for(auto & perk : state.rules["skills"][std::string(SKILL)]["perks"].Vector())
		if(perk["id"].String() == BASIC_PLANNED)
			perk["effect"]["status"].String() = "planned";
	const auto planned = newHorizonsPerkAvailability::evaluatePerk(
		state, SKILL, 3, definition(state, BASIC_PLANNED));
	EXPECT_EQ(planned.status, newHorizonsPerkAvailability::Status::UNAVAILABLE);
	EXPECT_EQ(planned.reason, newHorizonsPerkAvailability::Reason::PLANNED_INACTIVE);

	const auto unknownSkill = newHorizonsPerkAvailability::evaluateTier(state, "new-horizons:unknown", 3, 1);
	EXPECT_EQ(unknownSkill.status, newHorizonsPerkAvailability::Status::UNAVAILABLE);
	EXPECT_EQ(unknownSkill.reason, newHorizonsPerkAvailability::Reason::NO_CATALOGUE);

	newHorizonsHeroes::PerkState legacy;
	const auto legacyTier = newHorizonsPerkAvailability::evaluateTier(legacy, SKILL, 3, 1);
	EXPECT_EQ(legacyTier.status, newHorizonsPerkAvailability::Status::UNAVAILABLE);
	EXPECT_EQ(legacyTier.reason, newHorizonsPerkAvailability::Reason::NO_CATALOGUE);
}

TEST(NewHorizonsPerkAvailability, TierSlotUsesRegistryTierRegardlessOfSavedSelectionOrder)
{
	auto state = makeState();
	state.selected = {
		{std::string(SKILL), std::string(EXPERT)},
		{std::string(SKILL), std::string(BASIC)},
		{std::string(SKILL), std::string(ADVANCED)}};
	ASSERT_NO_THROW(state.validate());

	const auto checkSelectedTiers = [&state]()
	{
		const std::array<std::pair<int, std::string_view>, 3> expected = {{{1, BASIC}, {2, ADVANCED}, {3, EXPERT}}};
		for(const auto & [tier, perkId] : expected)
		{
			const auto slot = newHorizonsPerkAvailability::evaluateTier(state, SKILL, 3, tier);
			ASSERT_TRUE(slot.selectedPerk);
			EXPECT_EQ(slot.selectedPerk->id, perkId);
			EXPECT_EQ(slot.status, newHorizonsPerkAvailability::Status::ACQUIRED);
		}
	};

	checkSelectedTiers();
	std::reverse(state.selected.begin(), state.selected.end());
	checkSelectedTiers();
}

TEST(NewHorizonsPerkAvailability, ReadOnlyEvaluationsDoNotMutateSavedState)
{
	auto state = makeState();
	state.select(std::string(SKILL), std::string(BASIC), 1);
	const auto rulesBefore = state.rules;
	const auto selectedBefore = state.selected;

	static_cast<void>(newHorizonsPerkAvailability::evaluatePerk(
		state, SKILL, 3, definition(state, ADVANCED)));
	static_cast<void>(newHorizonsPerkAvailability::evaluateTier(state, SKILL, 3, 2));

	EXPECT_EQ(state.rules, rulesBefore);
	EXPECT_EQ(state.selected, selectedBefore);
}

TEST(NewHorizonsPerkAvailability, R2SameTierAlternativesRemainAvailableUntilCap)
{
	auto state = makeR2State();
	ASSERT_TRUE(newHorizonsHeroes::usesR2PerkOffers(state.rules));
	state.select(std::string(SKILL), std::string(BASIC), 1);
	EXPECT_EQ(newHorizonsPerkAvailability::evaluatePerk(
		state, SKILL, 1, definition(state, BASIC_ALTERNATIVE)).status,
		newHorizonsPerkAvailability::Status::AVAILABLE);
	state.select(std::string(SKILL), std::string(BASIC_ALTERNATIVE), 1);
	// Two Basic perks still do not substitute for an Advanced-tier perk.
	EXPECT_EQ(newHorizonsPerkAvailability::evaluatePerk(
		state, SKILL, 3, definition(state, EXPERT)).reason,
		newHorizonsPerkAvailability::Reason::EARLIER_TIER_MISSING);
	state.select(std::string(SKILL), std::string(ADVANCED), 2);
	EXPECT_EQ(newHorizonsPerkAvailability::evaluatePerk(
		state, SKILL, 3, definition(state, EXPERT)).reason,
		newHorizonsPerkAvailability::Reason::PER_SKILL_CAP);
}

TEST(NewHorizonsPerkAvailability, StrictCurrentOccupiedTierMatchesAuthoritativeAcquisitionPredicate)
{
	newHorizonsHeroes::PerkState state;
	state.rules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	ASSERT_TRUE(newHorizonsHeroes::usesStrictPerkProgression(state.rules));
	state.select(std::string(SKILL), std::string(BASIC), 1);
	const auto before = state.toJson();
	EXPECT_FALSE(state.canAcquirePerk(std::string(SKILL), std::string(BASIC_ALTERNATIVE), 3));
	const auto occupied = newHorizonsPerkAvailability::evaluatePerk(state, SKILL, 3,
		definition(state, BASIC_ALTERNATIVE));
	EXPECT_EQ(occupied.status, newHorizonsPerkAvailability::Status::LOCKED);
	EXPECT_EQ(occupied.reason, newHorizonsPerkAvailability::Reason::TIER_OCCUPIED);
	EXPECT_TRUE(state.canAcquirePerk(std::string(SKILL), std::string(ADVANCED), 3));
	EXPECT_EQ(newHorizonsPerkAvailability::evaluatePerk(state, SKILL, 3,
		definition(state, ADVANCED)).status, newHorizonsPerkAvailability::Status::AVAILABLE);
	EXPECT_EQ(newHorizonsPerkAvailability::evaluatePerk(state, SKILL, 3,
		definition(state, EXPERT)).reason, newHorizonsPerkAvailability::Reason::EARLIER_TIER_MISSING);
	EXPECT_EQ(state.toJson(), before);
}

TEST(NewHorizonsPerkAvailability, StrictExceptionalExpertSlotsFollowBasicAdvancedExpertWithoutRepeatedTier)
{
	newHorizonsHeroes::PerkState state;
	state.rules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	ASSERT_TRUE(newHorizonsHeroes::usesStrictPerkProgression(state.rules));
	const std::array<std::string_view, 3> selected = {BASIC, ADVANCED, EXPERT};
	for(size_t index = 0; index < selected.size(); ++index)
	{
		ASSERT_TRUE(state.canAcquirePerk(std::string(SKILL), std::string(selected[index]), 3));
		state.select(std::string(SKILL), std::string(selected[index]), 3);
		const auto slot = newHorizonsPerkAvailability::evaluateTier(state, SKILL, 3, index + 1);
		ASSERT_TRUE(slot.selectedPerk);
		EXPECT_EQ(slot.selectedPerk->id, selected[index]);
		EXPECT_EQ(slot.requiredRank, index + 1);
		EXPECT_EQ(slot.status, newHorizonsPerkAvailability::Status::ACQUIRED);
	}
	const auto capped = newHorizonsPerkAvailability::evaluatePerk(state, SKILL, 3,
		definition(state, ADVANCED_ALTERNATIVE));
	EXPECT_EQ(capped.status, newHorizonsPerkAvailability::Status::LOCKED);
	EXPECT_EQ(capped.reason, newHorizonsPerkAvailability::Reason::PER_SKILL_CAP);
	EXPECT_FALSE(state.canAcquirePerk(std::string(SKILL), std::string(ADVANCED_ALTERNATIVE), 3));
}

TEST(NewHorizonsPerkAvailability, R2SlotsDisplayAllSelectionsIncludingRepeatedTier)
{
	auto state = makeR2State();
	state.select(std::string(SKILL), std::string(BASIC), 1);
	state.select(std::string(SKILL), std::string(BASIC_ALTERNATIVE), 1);
	state.select(std::string(SKILL), std::string(ADVANCED), 2);
	const auto before = state.toJson();
	const std::array<std::string_view, 3> expected = {BASIC, BASIC_ALTERNATIVE, ADVANCED};
	for(size_t index = 0; index < expected.size(); ++index)
	{
		const auto slot = newHorizonsPerkAvailability::evaluateSelectionSlot(state, SKILL, 2, index);
		ASSERT_TRUE(slot.selectedPerk);
		EXPECT_EQ(slot.selectedPerk->id, expected[index]);
		EXPECT_EQ(slot.requiredRank, index == 2 ? 2 : 1);
		EXPECT_EQ(slot.status, newHorizonsPerkAvailability::Status::ACQUIRED);
	}
	EXPECT_EQ(state.toJson(), before);
	EXPECT_EQ(newHorizonsPerkAvailability::evaluateSelectionSlot(state, SKILL, 2, 3).status,
		newHorizonsPerkAvailability::Status::UNAVAILABLE);
}

TEST(NewHorizonsPerkAvailability, R2EmptySlotReportsCapacityWithoutInventingTier)
{
	auto state = makeR2State();
	state.select(std::string(SKILL), std::string(BASIC), 1);
	const auto empty = newHorizonsPerkAvailability::evaluateSelectionSlot(state, SKILL, 1, 1);
	EXPECT_FALSE(empty.selectedPerk);
	EXPECT_EQ(empty.requiredRank, 0);
	EXPECT_EQ(empty.status, newHorizonsPerkAvailability::Status::AVAILABLE);
	EXPECT_EQ(newHorizonsPerkAvailability::evaluateSelectionSlot(state, SKILL, 0, 0).status,
		newHorizonsPerkAvailability::Status::ACQUIRED);
	const auto unlearned = newHorizonsPerkAvailability::evaluateSelectionSlot(makeR2State(), SKILL, 0, 0);
	EXPECT_EQ(unlearned.reason, newHorizonsPerkAvailability::Reason::SKILL_NOT_LEARNED);
}
