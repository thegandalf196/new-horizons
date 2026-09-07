/*
 * NewHorizonsMasteryStateTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsMasteryState.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <stdexcept>

namespace
{
newHorizonsHeroes::MasteryState state()
{
	newHorizonsHeroes::MasteryState result;
	result.rules = JsonNode(JsonPath::builtin("config/newHorizonsMasteries"));
	return result;
}
}

TEST(NewHorizonsMasteryState, NewlyExpertMustWaitForNextLevelAndNoRankFourIsProduced)
{
	auto saved = state();
	saved.captureBeforeLevel(2, 2);
	// Becoming Expert during this level's normal skill choice cannot rewrite the latch.
	EXPECT_FALSE(saved.prepareOffer(ObjectInstanceID(7), PlayerColor(0), 2));
	saved.captureBeforeLevel(3, 3);
	const auto pending = saved.prepareOffer(ObjectInstanceID(7), PlayerColor(0), 3);
	ASSERT_TRUE(pending);
	EXPECT_EQ(pending->skill, SecondarySkill::ARTILLERY);
	EXPECT_EQ(pending->options.size(), 3u);
}

TEST(NewHorizonsMasteryState, InvalidRepliesPreservePendingOfferAndDuplicateAcceptanceFails)
{
	using namespace newHorizonsHeroes;
	auto saved = state();
	saved.captureBeforeLevel(2, 3);
	const auto pending = saved.prepareOffer(ObjectInstanceID(7), PlayerColor(0), 2);
	ASSERT_TRUE(pending);
	saved.applyOffer(*pending);
	EXPECT_EQ(saved.accept(ObjectInstanceID(8), PlayerColor(0), 1, 2, 3, 0), MasteryReplyError::WRONG_HERO);
	EXPECT_EQ(saved.accept(ObjectInstanceID(7), PlayerColor(1), 1, 2, 3, 0), MasteryReplyError::WRONG_PLAYER);
	EXPECT_EQ(saved.accept(ObjectInstanceID(7), PlayerColor(0), 1, 2, 3, -1), MasteryReplyError::INVALID_CHOICE);
	ASSERT_TRUE(saved.pending);
	EXPECT_TRUE(saved.selected.empty());
	EXPECT_EQ(saved.lastSequence, 1);
	EXPECT_EQ(saved.accept(ObjectInstanceID(7), PlayerColor(0), 1, 2, 3, 2), MasteryReplyError::NONE);
	ASSERT_EQ(saved.selected.size(), 1u);
	EXPECT_EQ(saved.selected.front().option.effect, MasteryEffect::ARTILLERY_REPAIR);
	EXPECT_EQ(saved.accept(ObjectInstanceID(7), PlayerColor(0), 1, 2, 3, 0), MasteryReplyError::STALE_OFFER);
	saved.captureBeforeLevel(3, 3);
	EXPECT_FALSE(saved.prepareOffer(ObjectInstanceID(7), PlayerColor(0), 3));
}

TEST(NewHorizonsMasteryState, PendingAndSelectedRoundtripKeepOriginalOffer)
{
	using namespace newHorizonsHeroes;
	auto original = state();
	original.captureBeforeLevel(2, 3);
	original.applyOffer(*original.prepareOffer(ObjectInstanceID(7), PlayerColor(0), 2));
	CMemorySerializer memory;
	memory.oser & original;
	MasteryState restored;
	memory.iser & restored;
	ASSERT_TRUE(restored.pending);
	EXPECT_EQ(restored.pending->options, original.pending->options);
	EXPECT_EQ(restored.accept(ObjectInstanceID(7), PlayerColor(0), 1, 2, 3, 1), MasteryReplyError::NONE);
	CMemorySerializer chosenMemory;
	chosenMemory.oser & restored;
	MasteryState chosen;
	chosenMemory.iser & chosen;
	ASSERT_EQ(chosen.selected.size(), 1u);
	EXPECT_EQ(chosen.selected.front().option.effect, MasteryEffect::ARTILLERY_PRECISION);
	EXPECT_FALSE(chosen.pending);
}

TEST(NewHorizonsMasteryState, CrossoverPreservesSavedOptionsAndRebindsOnlyIdentityWithFreshSequence)
{
	using namespace newHorizonsHeroes;
	auto original = state();
	original.captureBeforeLevel(2, 3);
	original.applyOffer(*original.prepareOffer(ObjectInstanceID(7), PlayerColor(0), 2));
	auto restored = MasteryState::fromJson(original.toJson());
	ASSERT_TRUE(restored.pending);
	EXPECT_EQ(restored.pending->options, original.pending->options);
	const auto rebound = restored.prepareOffer(ObjectInstanceID(8), PlayerColor(1), 2);
	ASSERT_TRUE(rebound);
	EXPECT_EQ(rebound->sequence, 2);
	EXPECT_EQ(rebound->options, original.pending->options);
	restored.applyOffer(*rebound);
	EXPECT_EQ(restored.accept(ObjectInstanceID(8), PlayerColor(1), 1, 2, 3, 0), MasteryReplyError::STALE_OFFER);
	EXPECT_EQ(restored.accept(ObjectInstanceID(8), PlayerColor(1), 2, 2, 3, 2), MasteryReplyError::NONE);
	const auto selected = MasteryState::fromJson(restored.toJson());
	ASSERT_EQ(selected.selected.size(), 1u);
	EXPECT_EQ(selected.selected.front().option.magnitude, 50);
	EXPECT_FALSE(selected.pending);
	EXPECT_TRUE(MasteryState::fromJson(JsonNode()).rules.isNull());
}

TEST(NewHorizonsMasteryState, CrossoverRejectsFractionalSequencesMissingFieldsAndContradictoryEligibility)
{
	using namespace newHorizonsHeroes;
	auto original = state();
	original.captureBeforeLevel(2, 3);
	original.applyOffer(*original.prepareOffer(ObjectInstanceID(7), PlayerColor(0), 2));
	const auto json = original.toJson();
	auto invalid = json;
	invalid["lastSequence"].Float() = 1.5;
	EXPECT_THROW(MasteryState::fromJson(invalid), std::runtime_error);
	invalid = json;
	invalid.Struct().erase("artilleryEligible");
	EXPECT_THROW(MasteryState::fromJson(invalid), std::runtime_error);
	ASSERT_EQ(original.accept(ObjectInstanceID(7), PlayerColor(0), 1, 2, 3, 0), MasteryReplyError::NONE);
	invalid = original.toJson();
	invalid["artilleryEligible"].Bool() = true;
	EXPECT_THROW(MasteryState::fromJson(invalid), std::runtime_error);
}

TEST(NewHorizonsMasteryState, AbsentRulesAndForgedOfferNeverAdoptDefaults)
{
	using namespace newHorizonsHeroes;
	MasteryState legacy;
	legacy.captureBeforeLevel(2, 3);
	EXPECT_FALSE(legacy.prepareOffer(ObjectInstanceID(7), PlayerColor(0), 2));
	EXPECT_TRUE(legacy.rules.isNull());
	auto active = state();
	active.captureBeforeLevel(2, 3);
	auto forged = *active.prepareOffer(ObjectInstanceID(7), PlayerColor(0), 2);
	forged.options[0].magnitude = 8;
	EXPECT_THROW(active.applyOffer(forged), std::runtime_error);
	EXPECT_FALSE(active.pending);
	EXPECT_EQ(active.lastSequence, 0);
}
