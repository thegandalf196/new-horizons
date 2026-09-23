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
#include "../../../lib/entities/hero/CHeroClass.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/json/JsonRandom.h"
#include "../../../lib/mapObjects/CRewardableObject.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/rewardable/Info.h"
#include "../../../lib/rewardable/Limiter.h"
#include "../../../lib/rewardable/Reward.h"

namespace
{
class SkillRewardObject : public CRewardableObject
{
public:
	using CRewardableObject::CRewardableObject;
	using Rewardable::Interface::grantRewardBeforeLevelup;
	using Rewardable::Interface::getAvailableRewards;
};

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

	void startNewHorizonsAsThane()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false)
			.playerActive(PlayerColor(0))
			.playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:thane")), PlayerColor(0))
			.heroGarrison({{CreatureID(0), 1}})
			.hero({7, 7, 0}, HeroTypeID(1), PlayerColor(1))
			.heroGarrison({{CreatureID(0), 1}});
		startWithMap(std::move(builder));

		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_EQ(attackerSideHero->getHeroClass()->getJsonKey(), "core:alchemist");
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

TEST_F(NewHorizonsRewardSkillFilterTest, ZeroWeightTeacherRewardIsFilteredAndFallbackMessagesRemainAvailable)
{
	startNewHorizonsAsThane();
	const auto wisdom = canonical("new-horizons:wisdom");
	ASSERT_TRUE(attackerSideHero->canLearnSkill());
	EXPECT_FALSE(attackerSideHero->canLearnSkill(wisdom));

	SkillRewardObject object(gameState().get());
	Rewardable::VisitInfo teacherReward;
	teacherReward.visitType = Rewardable::EEventType::EVENT_FIRST_VISIT;
	teacherReward.reward.secondary[wisdom] = 1;
	object.configuration.info.push_back(teacherReward);

	Rewardable::VisitInfo scholarFallback;
	scholarFallback.visitType = Rewardable::EEventType::EVENT_FIRST_VISIT;
	scholarFallback.reward.primary[static_cast<size_t>(PrimarySkill::ATTACK)] = 1;
	object.configuration.info.push_back(scholarFallback);

	Rewardable::VisitInfo witchHutExplanation;
	witchHutExplanation.visitType = Rewardable::EEventType::EVENT_NOT_AVAILABLE;
	object.configuration.info.push_back(witchHutExplanation);

	EXPECT_EQ(object.getAvailableRewards(attackerSideHero, Rewardable::EEventType::EVENT_FIRST_VISIT),
		(std::vector<ui32>{1}));
	EXPECT_EQ(object.getAvailableRewards(attackerSideHero, Rewardable::EEventType::EVENT_NOT_AVAILABLE),
		(std::vector<ui32>{2}));
}

TEST_F(NewHorizonsRewardSkillFilterTest, MixedRewardWithForbiddenSkillRemainsAvailable)
{
	startNewHorizonsAsThane();
	const auto wisdom = canonical("new-horizons:wisdom");
	ASSERT_FALSE(attackerSideHero->canLearnSkill(wisdom));

	SkillRewardObject object(gameState().get());
	Rewardable::VisitInfo mixedReward;
	mixedReward.visitType = Rewardable::EEventType::EVENT_FIRST_VISIT;
	mixedReward.reward.resources[GameResID::GOLD] = 100;
	mixedReward.reward.secondary[wisdom] = 1;
	object.configuration.info.push_back(mixedReward);

	EXPECT_EQ(object.getAvailableRewards(attackerSideHero, Rewardable::EEventType::EVENT_FIRST_VISIT),
		(std::vector<ui32>{0}));
}

TEST_F(NewHorizonsRewardSkillFilterTest, PureTeacherRewardRemainsAvailableWhenAnySkillIsLearnable)
{
	startNewHorizonsAsThane();
	const auto wisdom = canonical("new-horizons:wisdom");
	const auto lightMagic = canonical("new-horizons:lightMagic");
	ASSERT_FALSE(attackerSideHero->canLearnSkill(wisdom));
	ASSERT_TRUE(attackerSideHero->canLearnSkill(lightMagic));

	SkillRewardObject object(gameState().get());
	Rewardable::VisitInfo teacherReward;
	teacherReward.visitType = Rewardable::EEventType::EVENT_FIRST_VISIT;
	teacherReward.reward.secondary[wisdom] = 1;
	teacherReward.reward.secondary[lightMagic] = 1;
	object.configuration.info.push_back(teacherReward);

	EXPECT_EQ(object.getAvailableRewards(attackerSideHero, Rewardable::EEventType::EVENT_FIRST_VISIT),
		(std::vector<ui32>{0}));
}

TEST_F(NewHorizonsRewardSkillFilterTest, NegativeSkillAdjustmentKeepsTeacherRewardAvailable)
{
	startNewHorizonsAsThane();
	const auto wisdom = canonical("new-horizons:wisdom");
	const auto discipline = canonical("new-horizons:discipline");
	ASSERT_FALSE(attackerSideHero->canLearnSkill(wisdom));
	attackerSideHero->setSecSkillLevel(discipline, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);

	SkillRewardObject object(gameState().get());
	Rewardable::VisitInfo teacherReward;
	teacherReward.visitType = Rewardable::EEventType::EVENT_FIRST_VISIT;
	teacherReward.reward.secondary[wisdom] = 1;
	teacherReward.reward.secondary[discipline] = -1;
	object.configuration.info.push_back(teacherReward);

	EXPECT_EQ(object.getAvailableRewards(attackerSideHero, Rewardable::EEventType::EVENT_FIRST_VISIT),
		(std::vector<ui32>{0}));
	object.grantRewardBeforeLevelup(*gameHandler, teacherReward, attackerSideHero);
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(wisdom), MasteryLevel::NONE);
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(discipline), MasteryLevel::NONE);
}

TEST_F(NewHorizonsRewardSkillFilterTest, MissingClassWeightCannotBeLearnedFromPureTeacherReward)
{
	startNewHorizonsAsThane();
	const auto lightMagic = canonical("new-horizons:lightMagic");
	auto & rules = const_cast<JsonNode &>(attackerSideHero->getPrimaryGrowthRules());
	rules["skillOfferWeights"].Struct().erase(SecondarySkill::encode(lightMagic.getNum()));
	ASSERT_FALSE(newHorizonsHeroes::skillOfferWeight(attackerSideHero->getPrimaryGrowthRules(), lightMagic));
	ASSERT_TRUE(attackerSideHero->canLearnSkill());
	EXPECT_FALSE(attackerSideHero->canLearnSkill(lightMagic));

	SkillRewardObject object(gameState().get());
	Rewardable::VisitInfo teacherReward;
	teacherReward.visitType = Rewardable::EEventType::EVENT_FIRST_VISIT;
	teacherReward.reward.secondary[lightMagic] = 1;
	object.configuration.info.push_back(teacherReward);

	EXPECT_TRUE(object.getAvailableRewards(attackerSideHero, Rewardable::EEventType::EVENT_FIRST_VISIT).empty());
	object.grantRewardBeforeLevelup(*gameHandler, teacherReward, attackerSideHero);
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(lightMagic), MasteryLevel::NONE);
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
