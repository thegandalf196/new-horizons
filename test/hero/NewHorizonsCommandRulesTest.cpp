/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CommandTestSupport.h"
#include <limits>
#include <stdexcept>

using namespace newHorizonsHeroes;

TEST(NewHorizonsCommandRules, NaturalProgressionUnlocksAndRepeatingEnhancementCycle)
{
	for(const auto interval : {3u, 4u})
	{
		const auto rules = commandTest::rules(interval);
		for(uint32_t milestone = 1; milestone <= 21; ++milestone)
		{
			const auto level = 1 + (milestone - 1) * interval;
			EXPECT_EQ(commandValue(rules, level, 0).effective(), milestone);
			if(level > 1)
				EXPECT_EQ(commandValue(rules, level - 1, 0).effective(), milestone - 1);
		}
	}
	for(int slot = 0; slot < 7; ++slot)
	{
		const auto locked = commandSlotView(1, SlotID(slot));
		EXPECT_EQ(locked.unlocked, slot < 3);
		EXPECT_EQ(locked.unlockCommand, slot < 3 ? 1 : slot - 1);
		EXPECT_TRUE(commandSlotView(5, SlotID(slot)).unlocked);
		EXPECT_EQ(commandSlotView(5, SlotID(slot)).enhancementTier, 0);
		EXPECT_EQ(commandSlotView(10, SlotID(slot)).enhancementTier, 1);
		EXPECT_EQ(commandSlotView(15, SlotID(slot)).enhancementTier, 2);
		EXPECT_EQ(commandSlotView(20, SlotID(slot)).enhancementTier, 3);
	}
	EXPECT_EQ(commandMilestoneRecipient(6), CommandRecipient::CORE_BLOCK);
	EXPECT_EQ(commandMilestoneRecipient(7), CommandRecipient::ELITE_1);
	EXPECT_EQ(commandMilestoneRecipient(8), CommandRecipient::ELITE_2);
	EXPECT_EQ(commandMilestoneRecipient(9), CommandRecipient::ELITE_3);
	EXPECT_EQ(commandMilestoneRecipient(10), CommandRecipient::CHAMPION);
	EXPECT_EQ(commandMilestoneRecipient(16), CommandRecipient::CORE_BLOCK);
	EXPECT_EQ(commandSlotView(6, SlotID(0)).grade, CommandSlotGrade::CORE);
	EXPECT_EQ(commandSlotView(6, SlotID(3)).enhancementTier, 0);
}

TEST(NewHorizonsCommandRules, AuthorityIsTotalAndEarlyCommandSixUsesRealHeroLevel)
{
	const auto rules = commandTest::rules();
	for(int rank = 0; rank <= 3; ++rank)
	{
		const auto value = commandValue(rules, 7, rank);
		EXPECT_EQ(value.natural, 3);
		EXPECT_EQ(value.authority, rank);
		EXPECT_EQ(value.effective(), 3 + rank);
	}
	EXPECT_EQ(commandValue(rules, 1, 1).effective(), 2);
	EXPECT_EQ(commandEnhancementValue(rules, CommandEnhancement::FORTUNE, 7), 1);
	EXPECT_EQ(commandValue(rules, std::numeric_limits<uint32_t>::max(), 3).effective(), 1431655768LL);
	EXPECT_THROW(commandValue(rules, 0, 0), std::invalid_argument);
	EXPECT_THROW(commandValue(rules, 1, 4), std::invalid_argument);
	EXPECT_THROW(commandSlotView(1, SlotID(7)), std::invalid_argument);
}

TEST(NewHorizonsCommandRules, CeilingBoundariesAndIndependentInitiativeSpeedAndDamageFields)
{
	const auto rules = commandTest::rules();
	const std::array<uint32_t, 6> levels = {10, 11, 20, 21, 30, 31};
	const std::array<int64_t, 6> tens = {1, 2, 2, 3, 3, 4};
	const std::array<int64_t, 6> twenties = {1, 1, 1, 2, 2, 2};
	for(size_t i = 0; i < levels.size(); ++i)
	{
		EXPECT_EQ(commandEnhancementValue(rules, CommandEnhancement::FORTUNE, levels[i]), tens[i]);
		EXPECT_EQ(commandEnhancementValue(rules, CommandEnhancement::READINESS, levels[i]), twenties[i]);
		EXPECT_EQ(commandEnhancementValue(rules, CommandEnhancement::ENDURANCE, levels[i]), 5 * tens[i]);
		EXPECT_EQ(commandEnhancementValue(rules, CommandEnhancement::WARDING, levels[i]), 5 * tens[i]);
	}
	const auto initiative = commandEnhancementModifiers(CommandEnhancement::READINESS, 2);
	EXPECT_EQ(initiative.initiative, 2);
	EXPECT_EQ(initiative.speed, 0);
	EXPECT_EQ(initiative.directAttackDamagePercent, 0);
	EXPECT_EQ(commandEnhancementModifiers(CommandEnhancement::MOBILITY, 2).initiative, 0);
	EXPECT_THROW(commandEnhancementValue(rules, static_cast<CommandEnhancement>(9), 10), std::invalid_argument);
}

TEST(NewHorizonsCommandRules, CapturesClassIntervalsAndNeverFillsAbsentPolicy)
{
	auto caps = commandTest::capabilities();
	ASSERT_TRUE(capturedCommandRules(caps));
	EXPECT_EQ(*capturedCommandRules(caps), commandTest::rules());
	const auto saved = caps;
	auto installation = commandTest::policy();
	installation["classLevelIntervals"]["core:knight"].Integer() = 5;
	EXPECT_EQ(capturedCommandRules(saved)->levelInterval, 3u);
	caps["armyCommand"] = resolveArmyCommandPolicy(installation, "core:knight");
	EXPECT_EQ(capturedCommandRules(caps)->levelInterval, 5u);
	EXPECT_FALSE(capturedCommandRules(JsonNode()));
	EXPECT_THROW(resolveArmyCommandPolicy(installation, "core:unknown"), std::runtime_error);
	caps["armyCommand"] = JsonNode();
	EXPECT_THROW(capturedCommandRules(caps), std::runtime_error);
	caps = commandTest::capabilities();
	caps["armyCommand"]["enhancements"]["armament"] = JsonNode(1);
	EXPECT_THROW(capturedCommandRules(caps), std::runtime_error);
	caps = commandTest::capabilities();
	caps["armyCommand"]["enhancements"]["fortune"]["levelDivisor"] = JsonNode(10.5);
	EXPECT_THROW(capturedCommandRules(caps), std::runtime_error);
	caps = commandTest::capabilities();
	caps["armyCommand"]["authorityRankBonuses"].Vector()[0] = JsonNode(1);
	EXPECT_THROW(capturedCommandRules(caps), std::runtime_error);
	EXPECT_THROW(validateArmyCommandSerialization(saved, false), std::runtime_error);
	EXPECT_NO_THROW(validateArmyCommandSerialization(saved, true));
}

TEST(NewHorizonsCommandRules, AuthorityPassivesFollowSlotGradeAndAccumulateWithoutOverflow)
{
	const auto rules = commandTest::rules();
	std::array<bool, 10> all;
	all.fill(true);
	const auto core = authoritySlotModifiers(rules, SlotID(0), all);
	EXPECT_EQ(core.attack, 2);
	EXPECT_EQ(core.defense, 2);
	EXPECT_EQ(core.maximumHealthPercent, 10);
	EXPECT_EQ(core.initiative, 1);
	EXPECT_EQ(core.directAttackDamagePercent, 15);
	const auto elite = authoritySlotModifiers(rules, SlotID(3), all);
	EXPECT_EQ(elite.attack, 2);
	EXPECT_EQ(elite.maximumHealthPercent, 0);
	EXPECT_EQ(elite.directAttackDamagePercent, 10);
	const auto champion = authoritySlotModifiers(rules, SlotID(6), all);
	EXPECT_EQ(champion.attack, 0);
	EXPECT_EQ(champion.speed, 1);
	EXPECT_EQ(champion.retaliations, 1);
	EXPECT_EQ(champion.magicalReductionPercent, 10);
	EXPECT_EQ(champion.directAttackDamagePercent, 10);
	CommandModifiers extreme;
	extreme.luck = std::numeric_limits<int64_t>::max();
	const auto previous = extreme;
	CommandModifiers addition;
	addition.initiative = 1;
	addition.luck = 1;
	EXPECT_THROW(extreme.add(addition), std::overflow_error);
	EXPECT_EQ(extreme, previous);
}
