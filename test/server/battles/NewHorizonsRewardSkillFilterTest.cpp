/*
 * NewHorizonsRewardSkillFilterTest.cpp, part of VCMI engine
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
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/json/JsonRandom.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/rewardable/Info.h"
#include "../../../lib/rewardable/Limiter.h"
#include "../../../lib/rewardable/Reward.h"

namespace
{
SecondarySkill canonical(const char * identity)
{
	return SecondarySkill(SecondarySkill::decode(identity));
}

JsonNode authoredSecondary(const char * identity)
{
	JsonNode value;
	value[identity].Integer() = 1;
	value.setModScope(ModScope::scopeBuiltin());
	return value;
}
}

class NewHorizonsRewardSkillFilterTest : public HeroCommandFixture
{
protected:
	void startNewHorizons()
	{
		startGame();
		ASSERT_TRUE(newHorizonsHeroes::usesRules(gameState()->getHeroDevelopmentRules()));
	}
};

TEST_F(NewHorizonsRewardSkillFilterTest, AuthoredLeadershipRewardUsesCanonicalDiscipline)
{
	startNewHorizons();
	GameRandomizer randomizer(*gameState());
	JsonRandom loader(gameState().get(), randomizer);

	const auto decoded = loader.loadSecondaries(authoredSecondary("core:leadership"), {});
	ASSERT_EQ(decoded.size(), 1u);
	EXPECT_EQ(decoded.begin()->first, canonical("new-horizons:discipline"));
	EXPECT_NE(decoded.begin()->first, SecondarySkill::LEADERSHIP);
}

TEST_F(NewHorizonsRewardSkillFilterTest, RetiredPerkSkillIsSkippedFromAuthoredRewards)
{
	startNewHorizons();
	GameRandomizer randomizer(*gameState());
	JsonRandom loader(gameState().get(), randomizer);

	const auto decoded = loader.loadSecondaries(authoredSecondary("core:mysticism"), {});
	EXPECT_TRUE(decoded.empty());

	JsonNode choices;
	choices.Vector().emplace_back(std::string("core:mysticism"));
	choices.setModScope(ModScope::scopeBuiltin());
	EXPECT_TRUE(loader.loadSecondaries(choices, {}).empty());
}

TEST_F(NewHorizonsRewardSkillFilterTest, RandomSecondaryRewardsNeverExposeRetiredLegacySkills)
{
	startNewHorizons();
	GameRandomizer randomizer(*gameState());
	JsonRandom loader(gameState().get(), randomizer);
	JsonNode randomChoice(JsonMap{});

	for(int i = 0; i < 256; ++i)
	{
		const auto selected = loader.loadSecondary(randomChoice, {});
		ASSERT_NE(selected, SecondarySkill::NONE);
		EXPECT_NE(selected, SecondarySkill::LEADERSHIP);
		EXPECT_NE(selected, SecondarySkill::MYSTICISM);
	}
}

TEST_F(NewHorizonsRewardSkillFilterTest, RewardPresentationAndLimitersNormalizeLegacyIdentity)
{
	startNewHorizons();
	const auto discipline = canonical("new-horizons:discipline");
	attackerSideHero->setSecSkillLevel(discipline, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);

	Rewardable::Reward reward;
	reward.secondary[SecondarySkill::LEADERSHIP] = 1;
	std::vector<Component> components;
	reward.loadComponents(components, attackerSideHero);
	ASSERT_EQ(components.size(), 1u);
	EXPECT_EQ(components.front().type, ComponentType::SEC_SKILL);
	EXPECT_EQ(components.front().subType.as<SecondarySkill>(), discipline);

	Rewardable::Limiter limiter;
	limiter.secondary[SecondarySkill::LEADERSHIP] = 1;
	EXPECT_TRUE(limiter.heroAllowed(attackerSideHero));

	Rewardable::Limiter retired;
	retired.secondary[SecondarySkill::MYSTICISM] = 1;
	EXPECT_FALSE(retired.heroAllowed(attackerSideHero));
}

TEST_F(NewHorizonsRewardSkillFilterTest, ConfiguredRewardStoresCanonicalIdentity)
{
	startNewHorizons();
	GameRandomizer randomizer(*gameState());
	JsonNode object;
	object["rewards"].Vector().resize(1);
	object["rewards"].Vector().front()["secondary"]["core:leadership"].Integer() = 1;
	object.setModScope(ModScope::scopeBuiltin());

	Rewardable::Info info;
	info.init(object, "newHorizonsRewardSkillFilter");
	Rewardable::Configuration configured;
	ASSERT_NO_THROW(info.configureObject(configured, randomizer, gameState().get()));
	ASSERT_EQ(configured.info.size(), 1u);
	ASSERT_EQ(configured.info.front().reward.secondary.size(), 1u);
	EXPECT_EQ(configured.info.front().reward.secondary.begin()->first,
		canonical("new-horizons:discipline"));
}

class LegacyRewardSkillFilterTest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * map) override
	{
		useCommands = false;
		HeroCommandFixture::mapLoaded(map);
	}
};

TEST_F(LegacyRewardSkillFilterTest, LegacyModeRetainsAuthoredCoreSkills)
{
	startGame();
	ASSERT_FALSE(newHorizonsHeroes::usesRules(gameState()->getHeroDevelopmentRules()));
	GameRandomizer randomizer(*gameState());
	JsonRandom loader(gameState().get(), randomizer);

	const auto leadership = loader.loadSecondaries(authoredSecondary("core:leadership"), {});
	ASSERT_EQ(leadership.size(), 1u);
	EXPECT_EQ(leadership.begin()->first, SecondarySkill::LEADERSHIP);

	const auto mysticism = loader.loadSecondaries(authoredSecondary("core:mysticism"), {});
	ASSERT_EQ(mysticism.size(), 1u);
	EXPECT_EQ(mysticism.begin()->first, SecondarySkill::MYSTICISM);
}
