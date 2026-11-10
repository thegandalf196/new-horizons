/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../AI/CommandEnhancementChoice.h"
#include "../../AI/Nullkiller2/Analyzers/CommandSkillValue.h"
#include "../../AI/Nullkiller2/Analyzers/HeroManager.h"
#include "../server/battles/BattleTestFixture.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"

namespace
{
using namespace newHorizonsHeroes;
CommandEnhancementOffer offer(CommandRecipient recipient = CommandRecipient::CORE_BLOCK)
{
	CommandEnhancementOffer result;
	result.recipient = recipient;
	for(size_t i = 0; i < result.options.size(); ++i)
		result.options[i] = {static_cast<CommandEnhancement>(i), 0};
	return result;
}
}

TEST(CommandEnhancementChoiceTest, UsesCapturedContributionAndStableOfferOrder)
{
	auto input = offer();
	const std::array army{commandAI::EnhancementStack{SlotID(0), 100, 5, 5}};
	EXPECT_EQ(commandAI::chooseEnhancement(input, army), 0);
	input.options[8].contribution = 1000;
	EXPECT_EQ(commandAI::chooseEnhancement(input, army), 8);
	input.options[0].contribution = 10;
	EXPECT_EQ(commandAI::chooseEnhancement(input, army), 0);
	input.acquisitionLevel = 400; // Neither this nor a later hero level reprices the offer.
	EXPECT_EQ(commandAI::chooseEnhancement(input, army), 0);
	std::swap(input.options[0], input.options[8]);
	EXPECT_EQ(commandAI::chooseEnhancement(input, army), 8);
}

TEST(CommandEnhancementChoiceTest, ScoresOnlyDesignatedPhysicalSlotsWithOneSharedCoreChoice)
{
	auto input = offer();
	input.options[1].contribution = 1;
	input.options[2].contribution = 1;
	const std::array army{
		commandAI::EnhancementStack{SlotID(0), 100, 5, 5, true},
		commandAI::EnhancementStack{SlotID(1), 100, 5, 5, true},
		commandAI::EnhancementStack{SlotID(2), 100, 5, 5, true},
		commandAI::EnhancementStack{SlotID(3), 100000, 5, 20, false}};
	EXPECT_EQ(commandAI::chooseEnhancement(input, army), 1);
	input.recipient = CommandRecipient::ELITE_1;
	EXPECT_EQ(commandAI::chooseEnhancement(input, army), 2);
	input.recipient = CommandRecipient::ELITE_2; // Empty award still resolves once.
	EXPECT_EQ(commandAI::chooseEnhancement(input, army), 1);
}

TEST(CommandEnhancementChoiceTest, DoesNotValueIneligibleMoraleLuckOrUnlimitedCounters)
{
	auto input = offer();
	input.options[2].contribution = 100;
	input.options[5].contribution = 100;
	input.options[6].contribution = 100;
	input.options[7].contribution = 5;
	const std::array army{commandAI::EnhancementStack{SlotID(0), 100, 5, 5, false, true, false, false}};
	EXPECT_EQ(commandAI::chooseEnhancement(input, army), 7);
}

TEST(CommandEnhancementChoiceTest, RejectsNegativeCapturedValuesAndOversizedArmy)
{
	auto input = offer();
	input.options[4].contribution = -1;
	EXPECT_THROW(commandAI::chooseEnhancement(input, std::span<const commandAI::EnhancementStack>()), std::invalid_argument);
	input.options[4].contribution = 0;
	const std::array<commandAI::EnhancementStack, 8> invalid{};
	EXPECT_THROW(commandAI::chooseEnhancement(input, invalid), std::invalid_argument);
}

TEST(CommandArmyUnlockValueTest, CountsEliteChampionAndRepeatingAwardsWithoutCreatureBudgets)
{
	EXPECT_FLOAT_EQ(NK2AI::commandArmyUnlockValue(1, 2), 1);
	EXPECT_FLOAT_EQ(NK2AI::commandArmyUnlockValue(4, 5), 2);
	EXPECT_FLOAT_EQ(NK2AI::commandArmyUnlockValue(5, 6), 0.5f);
	EXPECT_FLOAT_EQ(NK2AI::commandArmyUnlockValue(9, 11), 1);
	EXPECT_FLOAT_EQ(NK2AI::commandArmyUnlockValue(3, 3), 0);
	EXPECT_FLOAT_EQ(NK2AI::commandArmyUnlockValue(3, 2), 0);
}

class CommandClassSkillScoreTest : public BattleTestFixture {};

TEST_F(CommandClassSkillScoreTest, AuthorityAddsTheSameImmediateRankUnlockValueToCapturedClassPrior)
{
	startGame();
	const SecondarySkill skill(SecondarySkill::decode("new-horizons:authority"));
	const auto rules = attackerSideHero->getArmyCommandRules();
	const auto weight = newHorizonsHeroes::skillOfferWeight(attackerSideHero->getPrimaryGrowthRules(), skill);
	ASSERT_TRUE(rules); ASSERT_TRUE(weight);
	for(int rank = 0; rank < 3; ++rank)
	{
		attackerSideHero->setSecSkillLevel(skill, rank, ChangeValueMode::ABSOLUTE);
		const auto before = commandValue(*rules, attackerSideHero->level, rank).effective();
		const auto after = commandValue(*rules, attackerSideHero->level, rank + 1).effective();
		const float expected = *weight * 0.2f + NK2AI::commandArmyUnlockValue(before, after);
		const auto main = NK2AI::commandClassSkillScore(*attackerSideHero, skill, true);
		const auto scout = NK2AI::commandClassSkillScore(*attackerSideHero, skill, false);
		ASSERT_TRUE(main); ASSERT_TRUE(scout);
		EXPECT_FLOAT_EQ(*main, expected);
		EXPECT_FLOAT_EQ(*scout, expected / 2);
	}
	attackerSideHero->setSecSkillLevel(skill, 3, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(NK2AI::commandClassSkillScore(*attackerSideHero, skill, true), std::optional<float>(*weight * 0.2f));
}

TEST_F(CommandClassSkillScoreTest, CurrentNamedClassSkillsHavePositiveScoresWithoutLegacyAliases)
{
	startGame();
	const NK2AI::SecondarySkillEvaluator core({std::make_shared<NK2AI::CommandClassSkillRule>(true)});
	for(const char * id : {"new-horizons:command", "new-horizons:offense", "new-horizons:armorer"})
	{
		const SecondarySkill skill(SecondarySkill::decode(id));
		const auto score = NK2AI::commandClassSkillScore(*attackerSideHero, skill, true);
		ASSERT_TRUE(score) << id;
		EXPECT_GT(*score, 0) << id;
		EXPECT_LE(*score, 2) << id;
		EXPECT_FLOAT_EQ(core.evaluateSecSkill(attackerSideHero, skill), *score) << id;
	}
}
