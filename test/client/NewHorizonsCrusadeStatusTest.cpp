/*
 * NewHorizonsCrusadeStatusTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "../StdInc.h"

#include "../../client/battle/NewHorizonsBattleStatus.h"
#include "../../lib/spells/CSpell.h"

namespace
{
using namespace newHorizonsBattleStatus;

std::shared_ptr<Bonus> makeSpellBonus(SpellID spell, BonusType type, int32_t value, BonusSubtypeID subtype,
	int32_t rounds, BonusSource source = BonusSource::SPELL_EFFECT)
{
	auto bonus = std::make_shared<Bonus>(BonusDuration::N_TURNS, type, source, value, BonusSourceID(spell), subtype);
	bonus->turnsRemain = rounds;
	return bonus;
}

std::vector<std::shared_ptr<Bonus>> fiveCrusadeBonuses(SpellID spell, int32_t rounds)
{
	return {
		makeSpellBonus(spell, BonusType::PRIMARY_SKILL, 4, BonusSubtypeID(PrimarySkill::ATTACK), rounds),
		makeSpellBonus(spell, BonusType::PRIMARY_SKILL, 3, BonusSubtypeID(PrimarySkill::DEFENSE), rounds),
		makeSpellBonus(spell, BonusType::STACKS_INITIATIVE_FLAT, 2, BonusSubtypeID(), rounds),
		makeSpellBonus(spell, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS, 1225, BonusSubtypeID(SpellSchool::ANY), rounds),
		makeSpellBonus(spell, BonusType::MINIMUM_MORALE, 0, BonusSubtypeID(), rounds),
	};
}
}

class NewHorizonsCrusadeStatusTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		crusadeId = SpellID::decode(std::string(CRUSADE_SPELL_KEY));
		if(crusadeId == SpellID::NONE)
			GTEST_SKIP() << "Requires the New Horizons Crusade spell registration";
	}

	SpellID crusadeId = SpellID::NONE;
};

TEST_F(NewHorizonsCrusadeStatusTest, ReadsAllFiveTimedBonusesAndFormatsFractionalReduction)
{
	const auto bonuses = fiveCrusadeBonuses(crusadeId, 3);
	const auto status = crusadeStatus(bonuses);

	ASSERT_TRUE(status.active());
	EXPECT_EQ(status.attackBonus, 4);
	EXPECT_EQ(status.defenseBonus, 3);
	EXPECT_EQ(status.initiativeBonus, 2);
	EXPECT_EQ(status.magicalDamageReductionBasisPoints, 1225);
	EXPECT_TRUE(status.protectsMoraleFromNegative);
	EXPECT_EQ(status.remainingRounds, 3);
	EXPECT_EQ(formatBasisPoints(status.magicalDamageReductionBasisPoints), "12.25%");

	const auto tooltip = crusadeTooltip("Crusade!", status);
	EXPECT_NE(tooltip.find("Attack: +4."), std::string::npos);
	EXPECT_NE(tooltip.find("Defense: +3."), std::string::npos);
	EXPECT_NE(tooltip.find("Initiative: +2 flat points."), std::string::npos);
	EXPECT_NE(tooltip.find("independent Magical Damage Reduction: 12.25%."), std::string::npos);
	EXPECT_NE(tooltip.find("Morale cannot fall below 0"), std::string::npos);
	EXPECT_NE(tooltip.find("3 rounds remaining"), std::string::npos);
}

TEST_F(NewHorizonsCrusadeStatusTest, CrusaderDurationUsesFourRounds)
{
	const auto status = crusadeStatus(fiveCrusadeBonuses(crusadeId, 4));

	ASSERT_TRUE(status.active());
	EXPECT_EQ(status.remainingRounds, 4);
	EXPECT_NE(crusadeTooltip("Crusade!", status).find("4 rounds remaining"), std::string::npos);
}

TEST_F(NewHorizonsCrusadeStatusTest, ExpiredAndUnrelatedBonusesDoNotProduceCrusadeStatus)
{
	auto expired = fiveCrusadeBonuses(crusadeId, 3);
	for(const auto & bonus : expired)
		bonus->turnsRemain = 0;
	EXPECT_FALSE(crusadeStatus(expired).active());

	const auto otherSource = makeSpellBonus(crusadeId, BonusType::PRIMARY_SKILL, 4,
		BonusSubtypeID(PrimarySkill::ATTACK), 3, BonusSource::ARTIFACT);
	const auto otherStat = makeSpellBonus(crusadeId, BonusType::PRIMARY_SKILL, 2,
		BonusSubtypeID(PrimarySkill::SPELL_POWER), 3);
	const auto otherReductionSubtype = makeSpellBonus(crusadeId, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS, 1500,
		BonusSubtypeID(SpellSchool::AIR), 3);
	const auto otherSpell = makeSpellBonus(SpellID::decode("core:magicArrow"), BonusType::PRIMARY_SKILL, 5,
		BonusSubtypeID(PrimarySkill::ATTACK), 3);
	const std::vector<std::shared_ptr<Bonus>> unrelated{otherSource, otherStat, otherReductionSubtype, otherSpell};
	EXPECT_FALSE(crusadeStatus(unrelated).active());
}

TEST_F(NewHorizonsCrusadeStatusTest, MixedDurationsReportTheShortestRemainingComponent)
{
	auto bonuses = fiveCrusadeBonuses(crusadeId, 4);
	bonuses.front()->turnsRemain = 3;

	const auto status = crusadeStatus(bonuses);
	ASSERT_TRUE(status.active());
	EXPECT_EQ(status.remainingRounds, 3);
}
