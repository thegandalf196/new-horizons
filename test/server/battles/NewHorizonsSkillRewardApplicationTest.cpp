/*
 * NewHorizonsSkillRewardApplicationTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/mapObjects/CRewardableObject.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

namespace
{
class SkillRewardObject : public CRewardableObject
{
public:
	using CRewardableObject::CRewardableObject;
	using Rewardable::Interface::grantRewardBeforeLevelup;
};
}

class NewHorizonsSkillRewardApplicationTest : public HeroCommandFixture {};

TEST_F(NewHorizonsSkillRewardApplicationTest, RawRewardAdjustsExistingEffectiveRankWithoutDowngradingOrIgnoringLoss)
{
	prepareCommands(true);
	const auto effective = newHorizonsMagic::replacementSkill(gameState()->getMagicRules(), SecondarySkill::FIRE_MAGIC);
	attackerSideHero->setSecSkillLevel(effective, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	SkillRewardObject object(gameState().get());
	Rewardable::VisitInfo info;
	info.reward.secondary[SecondarySkill::FIRE_MAGIC] = 1;
	// Actual Rewardable::Interface calculation and authoritative game callbacks;
	// not an asserted graphical visit or bypass of player choice validation.
	object.grantRewardBeforeLevelup(*gameHandler, info, attackerSideHero);
	ASSERT_EQ(attackerSideHero->getSecSkillLevel(effective), MasteryLevel::EXPERT);
	if(effective != SecondarySkill::FIRE_MAGIC)
		EXPECT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill::FIRE_MAGIC), 0);

	info.reward.secondary[SecondarySkill::FIRE_MAGIC] = -1;
	object.grantRewardBeforeLevelup(*gameHandler, info, attackerSideHero);
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(effective), MasteryLevel::ADVANCED);
	if(effective != SecondarySkill::FIRE_MAGIC)
		EXPECT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill::FIRE_MAGIC), 0);
}
