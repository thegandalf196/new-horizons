/*
 * NewHorizonsSkillRewardTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/json/JsonRandom.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/rewardable/Limiter.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

class NewHorizonsSkillRewardTest : public HeroCommandFixture
{
protected:
	SecondarySkill prepareSchool()
	{
		prepareCommands(true);
		return newHorizonsMagic::replacementSkill(gameState()->getMagicRules(), SecondarySkill::FIRE_MAGIC);
	}
};

TEST_F(NewHorizonsSkillRewardTest, AuthoredRewardAndLimiterKeysResolveBeforeDisplay)
{
	const auto expected = prepareSchool();
	JsonRandom randomizer(gameState().get(), *gameHandler->randomizer);
	JsonNode data;
	data["core:fireMagic"].Integer() = 1;
	data.setModScope(ModScope::scopeBuiltin());
	const auto decoded = randomizer.loadSecondaries(data, {});
	ASSERT_EQ(decoded.size(), 1u);
	EXPECT_EQ(decoded.begin()->first, expected);
	EXPECT_EQ(decoded.begin()->second, 1);
}

TEST_F(NewHorizonsSkillRewardTest, AuthoredSkillChoicesAndVariablesUseSameSavedReplacement)
{
	const auto expected = prepareSchool();
	JsonKeyExtractor extractor(gameState().get());
	JsonNode choice;
	choice["anyOf"].Vector().emplace_back(std::string("core:fireMagic"));
	choice.setModScope(ModScope::scopeBuiltin());
	const std::set<SecondarySkill> allowed{expected};
	EXPECT_EQ(extractor.filterKeys(choice, allowed), allowed);
	const JsonKeyExtractor::Variables variables{{"secondarySkill@mapReward", SecondarySkill::FIRE_MAGIC}};
	EXPECT_EQ(extractor.decodeKey<SecondarySkill>(ModScope::scopeBuiltin(), "@mapReward", variables), expected);
}

TEST_F(NewHorizonsSkillRewardTest, AuthoritativeRewardCallbackGrantsEffectiveSkill)
{
	const auto expected = prepareSchool();
	ASSERT_EQ(attackerSideHero->getSecSkillLevel(expected), 0);
	// This is the real server callback used by Rewardable::Interface, not a
	// player choice bypass and not a claimed graphical Scholar visit.
	gameHandler->changeSecSkill(attackerSideHero, SecondarySkill::FIRE_MAGIC, 1, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(expected), 1);
	if(expected != SecondarySkill::FIRE_MAGIC)
		EXPECT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill::FIRE_MAGIC), 0);
}

TEST_F(NewHorizonsSkillRewardTest, AuthoredOldSkillRequirementRecognizesMigratedSchool)
{
	const auto expected = prepareSchool();
	attackerSideHero->setSecSkillLevel(expected, 1, ChangeValueMode::ABSOLUTE);
	Rewardable::Limiter limiter;
	limiter.secondary[SecondarySkill::FIRE_MAGIC] = 1;
	EXPECT_TRUE(limiter.heroAllowed(attackerSideHero));
	attackerSideHero->setSecSkillLevel(expected, 0, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(limiter.heroAllowed(attackerSideHero));
}
