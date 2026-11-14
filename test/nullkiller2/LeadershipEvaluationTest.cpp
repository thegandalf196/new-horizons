/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../AI/PerkChoice.h"
#include "../../AI/Nullkiller2/Helpers/LeadershipEvaluation.h"
#include "../../AI/Nullkiller2/Helpers/ArmyFormation.h"
#include "../../AI/Nullkiller2/Analyzers/CommandSkillValue.h"
#include "../../AI/Nullkiller2/Analyzers/ArmyManager.h"
#include "../../lib/entities/hero/NewHorizonsLeadership.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../server/battles/BattleTestFixture.h"
#include <limits>
#include <stdexcept>

TEST(LeadershipArmyValue, KeepsFullDurabilityWhileDiscountingOffense)
{
	EXPECT_EQ(NK2AI::leadershipAI::powerWithEfficiency(10000, 1), 10000u);
	EXPECT_EQ(NK2AI::leadershipAI::powerWithEfficiency(10000, 0.25), 5000u);
	EXPECT_EQ(NK2AI::leadershipAI::powerWithEfficiency(0, 0.25), 0u);
	EXPECT_EQ(NK2AI::leadershipAI::powerWithEfficiency(std::numeric_limits<uint64_t>::max(), 1),
		std::numeric_limits<uint64_t>::max());
	EXPECT_THROW(NK2AI::leadershipAI::powerWithEfficiency(1, -0.1), std::invalid_argument);
	EXPECT_THROW(NK2AI::leadershipAI::powerWithEfficiency(1, 1.1), std::invalid_argument);
	EXPECT_THROW(NK2AI::leadershipAI::powerWithEfficiency(1,
		std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
}

class LeadershipArmyValueTest : public BattleTestFixture
{
protected:
	void prepare(CreatureID creature)
	{
		startGame();
		ASSERT_TRUE(newHorizonsHeroes::usesLeadershipEfficiency(attackerSideHero->getCapabilityRules()));
		attackerSideHero->clearSlots();
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creature, 10000));
	}
};

TEST_F(LeadershipArmyValueTest, OversizedStacksCanTransferMergeAndSplitWithoutCapacityFragmentation)
{
	const CreatureID creature(CreatureID::decode("core:pikeman"));
	ASSERT_NO_FATAL_FAILURE(prepare(creature));
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(6), creature, 20000));
	ASSERT_FALSE(attackerSideHero->getLeadershipSlotCapacity(creature));
	EXPECT_TRUE(NK2AI::armyFormation::canReceiveStack(attackerSideHero, SlotID(6), creature, 1000000));
	EXPECT_TRUE(NK2AI::armyFormation::canMergeOrSwapStacks(attackerSideHero, attackerSideHero, SlotID(0), SlotID(6)));
	EXPECT_TRUE(NK2AI::armyFormation::canSplitStack(attackerSideHero, attackerSideHero, SlotID(0), SlotID(5), 5000));
	EXPECT_EQ(NK2AI::armyFormation::maxLegalTransferCount(attackerSideHero, attackerSideHero, SlotID(0), SlotID(6)), 10000);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 10000);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(6)), 20000);
}

TEST_F(LeadershipArmyValueTest, AuthorityChoiceValuesEligibleCreaturesWithoutChangingRanksOrArmy)
{
	const CreatureID creature(CreatureID::decode("core:pikeman"));
	ASSERT_NO_FATAL_FAILURE(prepare(creature));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:authority")),
		1, ChangeValueMode::ABSOLUTE);
	const auto rank = attackerSideHero->getPerkSkillRank("new-horizons:authority");
	const auto selections = attackerSideHero->getPerkState().selected;
	newHorizonsHeroes::PerkOfferCandidate core{{"new-horizons:authority", "new-horizons:authority.massedRanks"}, {}, {}, 1};
	auto elite = core;
	elite.selection.perkId = "new-horizons:authority.veteranCadre";
	EXPECT_GT(NK2AI::leadershipAI::perkGain(*attackerSideHero, core), 0);
	EXPECT_FLOAT_EQ(NK2AI::leadershipAI::perkGain(*attackerSideHero, elite), 0);
	EXPECT_LT(NK2AI::leadershipAI::armyStrength(attackerSideHero, attackerSideHero), attackerSideHero->getArmyStrength());
	EXPECT_EQ(NK2AI::leadershipAI::armyStrength(nullptr, attackerSideHero), attackerSideHero->getArmyStrength());
	EXPECT_EQ(attackerSideHero->getPerkSkillRank("new-horizons:authority"), rank);
	EXPECT_EQ(attackerSideHero->getPerkState().selected, selections);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 10000);
}

TEST_F(LeadershipArmyValueTest, NecromancyAndAuthorityTrainingUseCurrentStackEfficiency)
{
	ASSERT_NO_FATAL_FAILURE(prepare(CreatureID(CreatureID::decode("core:skeleton"))));
	auto modifiers = newHorizonsHeroes::captureLeadershipModifiers(*attackerSideHero);
	modifiers.necromancyRank = 1;
	const auto first = NK2AI::leadershipAI::modifierGain(*attackerSideHero, modifiers);
	EXPECT_GT(first, 0);
	modifiers.lordOfTheDead = true;
	EXPECT_GT(NK2AI::leadershipAI::modifierGain(*attackerSideHero, modifiers), first);
	const SecondarySkill authority(SecondarySkill::decode("new-horizons:authority"));
	EXPECT_TRUE(NK2AI::commandClassSkillScore(*attackerSideHero, authority, true));
}

TEST_F(LeadershipArmyValueTest, MandatoryFullTierOfferUsesDeterministicEligibleEntry)
{
	ASSERT_NO_FATAL_FAILURE(prepare(CreatureID(CreatureID::decode("core:pikeman"))));
	const SecondarySkill authority(SecondarySkill::decode("new-horizons:authority"));
	attackerSideHero->setSecSkillLevel(authority, 1, ChangeValueMode::ABSOLUTE);
	const auto offered = attackerSideHero->getPerkState().candidatesForSkillTier("new-horizons:authority", 1);
	ASSERT_EQ(offered.size(), 4u);
	const auto selected = heroPerkAI::firstEligible(*attackerSideHero, offered);
	ASSERT_TRUE(selected);
	EXPECT_EQ(*selected, 0u);
	EXPECT_EQ(heroPerkAI::firstEligible(*attackerSideHero, offered), selected);
	attackerSideHero->applyPerkSelection(offered[*selected].selection);
	EXPECT_FALSE(heroPerkAI::firstEligible(*attackerSideHero, offered));
}

TEST_F(LeadershipArmyValueTest, CombiningTechnicalMaximumStacksPreservesWideCounts)
{
	const CreatureID creature(CreatureID::decode("core:pikeman"));
	ASSERT_NO_FATAL_FAILURE(prepare(creature));
	const auto maximum = std::numeric_limits<TQuantity>::max();
	CCreatureSet first, second;
	ASSERT_TRUE(first.setCreature(SlotID(0), creature, maximum));
	ASSERT_TRUE(second.setCreature(SlotID(0), creature, maximum));
	NK2AI::ArmyManager manager(nullptr, nullptr);
	const auto combined = manager.getSortedSlots(&first, &second);
	ASSERT_EQ(combined.size(), 2u);
	EXPECT_EQ(int64_t(combined[0].count) + combined[1].count, int64_t(maximum) * 2);
	const uint64_t expected = uint64_t(creature.toCreature()->getAIValue()) * uint64_t(maximum);
	EXPECT_EQ(manager.evaluateStackPower(creature.toCreature(), maximum), expected);
	EXPECT_EQ(combined[0].power, expected);
	EXPECT_EQ(combined[1].power, expected);
	NK2AI::SlotInfo supply{};
	NK2AI::accumulateArmySupply(supply, creature.toCreature(), maximum);
	NK2AI::accumulateArmySupply(supply, creature.toCreature(), maximum);
	EXPECT_EQ(supply.count, maximum);
	EXPECT_EQ(supply.power, expected * 2);
	EXPECT_EQ(first.getStackCount(SlotID(0)), maximum);
	EXPECT_EQ(second.getStackCount(SlotID(0)), maximum);
}

TEST(LeadershipArmyValue, WideArithmeticPreservesOrdinaryValuesAndSaturatesTotals)
{
	using namespace NK2AI::leadershipAI;
	const auto maximum = std::numeric_limits<uint64_t>::max();
	EXPECT_EQ(addPower(123, 456), 579u);
	EXPECT_EQ(addPower(maximum - 4, 5), maximum);
	EXPECT_EQ(scalePower(maximum, 2), maximum);
	EXPECT_EQ(scalePowerRatio(12345, 7, 11), 12345u * 7 / 11);
	EXPECT_EQ(scalePowerRatio(maximum, 2, 1), maximum);
	EXPECT_EQ(scalePowerRatio(maximum, 1, 3), maximum / 3);
	EXPECT_THROW(scalePowerRatio(1, 1, 0), std::invalid_argument);
	NK2AI::ArmyUpgradeInfo upgrade;
	upgrade.addArmyToGet({{nullptr, 0, maximum}, {nullptr, 0, 1}});
	EXPECT_EQ(upgrade.upgradeValue, std::numeric_limits<int64_t>::max());
}
