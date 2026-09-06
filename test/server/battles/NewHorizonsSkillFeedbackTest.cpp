/*
 * NewHorizonsSkillFeedbackTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/mapObjects/Quest.h"
#include "../../../lib/networkPacks/Component.h"
#include "../../../lib/rewardable/Limiter.h"
#include "../../../lib/rewardable/Reward.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

class NewHorizonsSkillFeedbackTest : public HeroCommandFixture {};

TEST_F(NewHorizonsSkillFeedbackTest, RawRewardAndRequirementComponentsNameEffectiveSkill)
{
	prepareCommands(true);
	const auto expected = newHorizonsMagic::replacementSkill(gameState()->getMagicRules(), SecondarySkill::FIRE_MAGIC);
	Rewardable::Limiter limiter;
	limiter.secondary[SecondarySkill::FIRE_MAGIC] = 1;
	Rewardable::Reward reward;
	reward.secondary[SecondarySkill::FIRE_MAGIC] = 1;
	std::vector<Component> components;
	limiter.loadComponents(components, attackerSideHero);
	ASSERT_EQ(components.size(), 1u);
	EXPECT_EQ(components.front().subType.as<SecondarySkill>(), expected);
	components.clear();
	reward.loadComponents(components, attackerSideHero);
	ASSERT_EQ(components.size(), 1u);
	EXPECT_EQ(components.front().subType.as<SecondarySkill>(), expected);
}

TEST_F(NewHorizonsSkillFeedbackTest, QuestTextNamesEffectiveRequirementWithoutNeedingHeroSelection)
{
	prepareCommands(true);
	const auto expected = newHorizonsMagic::replacementSkill(gameState()->getMagicRules(), SecondarySkill::FIRE_MAGIC);
	Quest quest;
	quest.mission.secondary[SecondarySkill::FIRE_MAGIC] = 1;
	quest.firstVisitText.appendRawString("%s");
	MetaString text;
	std::vector<Component> components;
	quest.getVisitText(gameState().get(), text, components, true, nullptr);
	EXPECT_NE(text.toString(LIBRARY->staticTexts()).find(expected.toSkill()->getNameTranslated()), std::string::npos);
	ASSERT_EQ(components.size(), 1u);
	EXPECT_EQ(components.front().subType.as<SecondarySkill>(), expected);
}
