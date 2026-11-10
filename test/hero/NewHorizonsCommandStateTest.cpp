/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CommandTestSupport.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <stdexcept>

using namespace newHorizonsHeroes;

TEST(NewHorizonsCommandState, EarlyAuthorityAwardsOnceAndNaturalCatchupDoesNotRegrant)
{
	const auto rules = commandTest::rules();
	CommandState state;
	EXPECT_EQ(state.capture(rules, 1, 0), 0u);
	EXPECT_EQ(state.capture(rules, 7, 3), 1u);
	ASSERT_TRUE(state.nextPending());
	EXPECT_EQ(state.nextPending()->milestone, 6);
	EXPECT_EQ(state.nextPending()->acquisitionLevel, 7u);
	state.select(rules, 6, CommandEnhancement::FORTUNE);
	EXPECT_EQ(state.getAwards().front().contribution, 1);
	EXPECT_EQ(state.capture(rules, 7, 0), 0u);
	EXPECT_EQ(state.capture(rules, 7, 3), 0u);
	EXPECT_EQ(state.capture(rules, 16, 0), 0u);
	EXPECT_EQ(state.getAwards().size(), 1u);
	EXPECT_EQ(state.getAwards().front().acquisitionLevel, 7u);
	const auto previous = state;
	EXPECT_THROW(state.select(rules, 6, CommandEnhancement::ENDURANCE), std::runtime_error);
	EXPECT_EQ(state, previous);
}

TEST(NewHorizonsCommandState, EachLevelRetainsItsEntitlementLevelThroughDeferredSelectionAndReload)
{
	const auto rules = commandTest::rules();
	CommandState state;
	for(uint32_t level = 1; level <= 31; ++level)
		state.capture(rules, level, 0);
	const std::array<uint32_t, 6> expected = {16, 19, 22, 25, 28, 31};
	ASSERT_EQ(state.getAwards().size(), expected.size());
	for(size_t i = 0; i < expected.size(); ++i)
		EXPECT_EQ(state.getAwards()[i].acquisitionLevel, expected[i]);
	state = CommandState::fromJson(state.toJson(rules), rules);
	for(int64_t milestone = 6; milestone <= 11; ++milestone)
		state.select(rules, milestone, CommandEnhancement::FORTUNE);
	const auto modifiers = state.modifiers(rules, 11, SlotID(0), {});
	EXPECT_EQ(modifiers.enhancements.luck, 6); // +2 at16 and +4 at31; no final-level rescaling.
	for(int slot : {1, 2})
		EXPECT_EQ(state.modifiers(rules, 11, SlotID(slot), {}).enhancements, modifiers.enhancements);
	EXPECT_EQ(state.modifiers(rules, 11, SlotID(3), {}).enhancements.luck, 2);
}

TEST(NewHorizonsCommandState, RankJumpQueuesOrderedMilestonesAndRejectionsAreAtomic)
{
	const auto rules = commandTest::rules();
	CommandState state;
	state.capture(rules, 13, 0);
	EXPECT_EQ(state.capture(rules, 13, 3), 3u);
	ASSERT_EQ(state.getAwards().size(), 3u);
	EXPECT_EQ(state.getAwards()[0].recipient, CommandRecipient::CORE_BLOCK);
	EXPECT_EQ(state.getAwards()[1].recipient, CommandRecipient::ELITE_1);
	EXPECT_EQ(state.getAwards()[2].recipient, CommandRecipient::ELITE_2);
	for(const auto & award : state.getAwards())
		EXPECT_EQ(award.acquisitionLevel, 13u);
	const auto previous = state;
	EXPECT_THROW(state.select(rules, 7, CommandEnhancement::FORTUNE), std::runtime_error);
	EXPECT_THROW(state.select(rules, 6, static_cast<CommandEnhancement>(99)), std::invalid_argument);
	EXPECT_THROW(state.capture(rules, 12, 3), std::runtime_error);
	EXPECT_EQ(state, previous);
	state.select(rules, 6, CommandEnhancement::ENDURANCE);
	EXPECT_EQ(state.modifiers(rules, 8, SlotID(2), {}).enhancements.maximumHealthPercent, 10);
	EXPECT_EQ(state.modifiers(rules, 8, SlotID(3), {}).enhancements.maximumHealthPercent, 0);
}

TEST(NewHorizonsCommandState, EnduranceAndWardingAddPerSourceAndWardingCannotBecomeNegativeDamage)
{
	const auto rules = commandTest::rules();
	for(const auto enhancement : {CommandEnhancement::ENDURANCE, CommandEnhancement::WARDING})
	{
		CommandState state;
		for(uint32_t level = 1; level <= 61; ++level)
		{
			state.capture(rules, level, 0);
			while(const auto pending = state.nextPending())
				state.select(rules, pending->milestone, enhancement);
		}
		const auto result = state.modifiers(rules, 21, SlotID(0), {});
		EXPECT_EQ(result.enhancements.maximumHealthPercent, enhancement == CommandEnhancement::ENDURANCE ? 90 : 0);
		EXPECT_EQ(result.enhancements.magicalReductionPercent, enhancement == CommandEnhancement::WARDING ? 90 : 0);
	}
	CommandSlotModifiers raw;
	raw.enhancements.magicalReductionPercent = 140;
	EXPECT_EQ(raw.boundedEnhancementWardingPercent(), 100);
	EXPECT_EQ(raw.enhancements.magicalReductionPercent, 140); // Receipt remains exact, final95% cap belongs downstream.
}

TEST(NewHorizonsCommandState, PendingAndChosenBinaryRoundtripAndUnsupportedWritesFailBeforeBytes)
{
	const auto rules = commandTest::rules();
	CommandState state;
	state.capture(rules, 13, 3);
	state.select(rules, 6, CommandEnhancement::WARDING);
	CMemorySerializer memory;
	state.serialize(memory.oser, rules);
	CommandState restored;
	restored.serialize(memory.iser, rules);
	EXPECT_EQ(restored, state);
	EXPECT_EQ(restored.nextPending()->acquisitionLevel, 13u);
	EXPECT_EQ(restored.capture(rules, 13, 3), 0u);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_NATIVE_RULES_REDESIGN;
	EXPECT_THROW(state.serialize(old.oser, rules), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	CommandState empty;
	EXPECT_THROW(empty.serialize(old.oser, rules), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	EXPECT_NO_THROW(empty.serialize(old.oser, std::nullopt));
	old.iser.version = ESerializationVersion::NEW_HORIZONS_NATIVE_RULES_REDESIGN;
	const auto previous = restored;
	EXPECT_THROW(restored.serialize(old.iser, rules), std::runtime_error);
	EXPECT_EQ(restored, previous);
	restored.serialize(old.iser, std::nullopt);
	EXPECT_TRUE(restored.empty());
}

TEST(NewHorizonsCommandState, JsonRejectsRescalingMissingHistoryGapsAndForgedRecipient)
{
	const auto rules = commandTest::rules();
	CommandState state;
	state.capture(rules, 13, 3);
	state.select(rules, 6, CommandEnhancement::FORTUNE);
	const auto valid = state.toJson(rules);
	EXPECT_EQ(CommandState::fromJson(valid, rules), state);
	EXPECT_THROW(CommandState::fromJson(JsonNode(), rules), std::runtime_error);
	EXPECT_THROW(CommandState::fromJson(valid, std::nullopt), std::runtime_error);
	auto invalid = valid;
	invalid["awards"].Vector()[0]["contribution"].Integer() = 99;
	EXPECT_THROW(CommandState::fromJson(invalid, rules), std::runtime_error);
	invalid = valid;
	invalid["awards"].Vector()[0]["recipient"].Integer() = 4;
	EXPECT_THROW(CommandState::fromJson(invalid, rules), std::runtime_error);
	invalid = valid;
	invalid["awards"].Vector()[1]["milestone"].Integer() = 8;
	EXPECT_THROW(CommandState::fromJson(invalid, rules), std::runtime_error);
	invalid = valid;
	invalid["awards"].Vector()[0]["acquisitionLevel"] = JsonNode(7.5);
	EXPECT_THROW(CommandState::fromJson(invalid, rules), std::runtime_error);
	invalid = valid;
	invalid["awards"].Vector()[0].Struct().erase("enhancement");
	EXPECT_THROW(CommandState::fromJson(invalid, rules), std::runtime_error);
	invalid = valid;
	invalid["awards"].Vector()[0]["enhancement"].String() = "armament";
	EXPECT_THROW(CommandState::fromJson(invalid, rules), std::runtime_error);
	invalid = valid;
	invalid["lastCaptureLevel"].Integer() = 100;
	EXPECT_THROW(CommandState::fromJson(invalid, rules), std::runtime_error);
}

TEST(NewHorizonsCommandState, MalformedBinaryHistoryDoesNotReplaceExistingState)
{
	const auto rules = commandTest::rules();
	CommandState state;
	state.capture(rules, 7, 3);
	state.select(rules, 6, CommandEnhancement::READINESS);
	const auto previous = state;
	CMemorySerializer malformed;
	int64_t highest = 6;
	uint32_t level = 7;
	std::vector<CommandEnhancementAward> forged = {{6, CommandRecipient::CHAMPION, 7, CommandEnhancement::READINESS, 1}};
	malformed.oser & highest;
	malformed.oser & level;
	malformed.oser & forged;
	EXPECT_THROW(state.serialize(malformed.iser, rules), std::runtime_error);
	EXPECT_EQ(state, previous);
}
