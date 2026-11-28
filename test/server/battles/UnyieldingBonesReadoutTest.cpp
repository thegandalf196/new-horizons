/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CastleSpecialtyFixture.h"
#include "../../../client/windows/CreatureSupplementalStats.h"
#include "../../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

namespace
{
struct ProtectionCase
{
	const char * hero;
	int level;
	bool historical;
	int basisPoints;
	const char * displayed;
};
class UnyieldingBonesReadoutTest : public CastleSpecialtyTest,
	public ::testing::WithParamInterface<ProtectionCase>
{
};
}

TEST_P(UnyieldingBonesReadoutTest, ArmyBattleCompositionAndControlUseCapturedProtection)
{
	const auto test = GetParam();
	if(test.historical)
		stripped = {"unyieldingBones"};
	ASSERT_NO_FATAL_FAILURE(specialist(test.hero, test.level, false));
	const SecondarySkill armorer(SecondarySkill::decode(std::string(newHorizonsCombatSkills::ARMORER_SKILL_ID)));
	ASSERT_TRUE(armorer.hasValue());
	attackerSideHero->setSecSkillLevel(armorer, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	defenderSideHero->setSecSkillLevel(armorer, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	defenderSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:ivor")));
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:skeleton"), 60));
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * subject = place(BattleSide::ATTACKER, "core:skeleton", BattleHex(3, 5), 60, 1000);
	ASSERT_TRUE(subject);
	const auto * army = attackerSideHero->getStackPtr(SlotID(0));
	ASSERT_TRUE(army);
	const auto readout = [&](const CStack * live)
	{
		return creatureSupplementalStats::query(*army, live, attackerSideHero, attackerSideHero->getMagicRules());
	};
	EXPECT_EQ(readout(nullptr).physicalReductionBasisPoints, test.basisPoints);
	EXPECT_EQ(readout(subject).physicalReductionBasisPoints, test.basisPoints);
	EXPECT_EQ(creatureSupplementalStats::percentageText(test.basisPoints), test.displayed);
	attackerSideHero->setSecSkillLevel(armorer, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	const int armorerPercent = newHorizonsCombatSkills::armorerReductionPercent(attackerSideHero);
	ASSERT_GT(armorerPercent, 0);
	const int combined = (10000 * 100 - (10000 - test.basisPoints) * (100 - armorerPercent) + 50) / 100;
	EXPECT_EQ(readout(nullptr).physicalReductionBasisPoints, combined);
	EXPECT_EQ(readout(subject).physicalReductionBasisPoints, combined);
	// Live protection follows the new controller; the army card keeps its owner.
	subject->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED,
		BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(battle()->battleGetOwnerHero(subject), defenderSideHero);
	EXPECT_EQ(readout(subject).physicalReductionBasisPoints, 0);
	EXPECT_EQ(readout(nullptr).physicalReductionBasisPoints, combined);
	auto cap = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::GENERAL_DAMAGE_REDUCTION,
		BonusSource::ARTIFACT, 100, BonusSourceID(ArtifactID(0)),
		BonusSubtypeID(BonusCustomSubtype::damageTypeAll), BonusValueType::ADDITIVE_VALUE);
	subject->addNewBonus(cap);
	EXPECT_EQ(readout(subject).physicalReductionBasisPoints,
		newHorizonsMagic::physicalDamageReductionCapPercent(attackerSideHero->getMagicRules()) * 100);
}

INSTANTIATE_TEST_SUITE_P(CurrentAndHistorical, UnyieldingBonesReadoutTest,
	::testing::Values(
		ProtectionCase{"core:xsi", 1, false, 500, "5%"},
		ProtectionCase{"core:xsi", 2, false, 550, "5.5%"},
		ProtectionCase{"core:xsi", 20, false, 1450, "14.5%"},
		ProtectionCase{"core:xsi", 20, true, 0, "0%"},
		ProtectionCase{"core:ivor", 20, false, 0, "0%"}));
