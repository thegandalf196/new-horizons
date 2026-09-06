/*
 * HeroCommandCloneTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/battle/BattleInfo.h"

class HeroCommandCloneTest : public HeroCommandFixture {};

TEST_F(HeroCommandCloneTest, RealCloneDoesNotCopyDoctrineButReceivesSubsequentSwitch)
{
	prepareCommands(true);
	attackerSideHero->addSpellToSpellbook(SpellID::CLONE);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 3, ChangeValueMode::ABSOLUTE);
	const auto * original = battle()->battleGetStackByID(battle()->battleActiveUnit()->unitId());
	ASSERT_TRUE(issue(HeroCommand::AGGRESSIVE));
	advanceRound();
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::CLONE;
	action.aimToUnit(original);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));

	const CStack * clone = nullptr;
	for(const auto * unit : battle()->battleGetAllStacks())
	{
		if(unit->isClone() && unit->unitSide() == BattleSide::ATTACKER)
			clone = unit;
	}
	ASSERT_NE(clone, nullptr);
	ASSERT_TRUE(clone->alive());
	EXPECT_TRUE(clone->getAllBonuses(Selector::sourceTypeSel(BonusSource::HERO_COMMAND))->empty());
	EXPECT_FALSE(original->getAllBonuses(Selector::sourceTypeSel(BonusSource::HERO_COMMAND))->empty());
	EXPECT_EQ(battle()->battleGetActiveDoctrine(BattleSide::ATTACKER), HeroCommand::AGGRESSIVE);

	advanceRound();
	ASSERT_TRUE(clone->alive());
	ASSERT_TRUE(issue(HeroCommand::DEFENSIVE));
	EXPECT_EQ(clone->getAllBonuses(Selector::sourceTypeSel(BonusSource::HERO_COMMAND))->size(), 2u);
	EXPECT_EQ(original->getAllBonuses(Selector::sourceTypeSel(BonusSource::HERO_COMMAND))->size(), 2u);
}
