/*
 * NewHorizonsBattleScholarAcademyTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../../mock/GameHandlerTestServer.h"
#include "../../mock/TinyH3MBuilder.h"
#include "../../mock/TinyMapGameTest.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/BonusEnum.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapObjects/TownBuildingInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/Component.h"
#include "../../../lib/rewardable/Configuration.h"
#include "../../../lib/rewardable/Info.h"
#include "../../../lib/rewardable/Reward.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../server/CGameHandler.h"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <vector>

namespace
{
using Rewardable::Reward;

class NewHorizonsBattleScholarAcademyTest : public TinyMapGameTest
{
protected:
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
	}

	void configurePlayer(PlayerSettings & settings) const override
	{
		if(settings.color == PlayerColor(0))
			settings.connectedPlayerIDs.clear();
	}

	static HeroTypeID heroType(const char * id)
	{
		return HeroTypeID(HeroTypeID::decode(id));
	}

	static int experiencePreview(const Rewardable::Reward & reward, const CGHeroInstance * hero)
	{
		std::vector<Component> components;
		reward.loadComponents(components, hero);
		const auto experience = std::ranges::find_if(components, [](const Component & component)
		{
			return component.type == ComponentType::EXPERIENCE;
		});
		if(experience == components.end() || !experience->value)
			return 0;
		return *experience->value;
	}

	static TExpType expectedPercentGrant(const CGHeroInstance & hero, int percent)
	{
		if(hero.level >= LIBRARY->heroh->maxSupportedLevel())
			return 0;
		const auto nextLevel = LIBRARY->heroh->reqExp(hero.level + 1);
		const TExpType gap = std::max<TExpType>(0, nextLevel - hero.exp);
		const TExpType rawPercent = (gap / 100) * percent + (gap % 100) * percent / 100;
		return hero.calculateXp(rawPercent);
	}
};

Rewardable::Reward parseConfiguredReward(IGameInfoCallback * callback, IGameRandomizer & randomizer,
	const std::optional<JsonNode> & percentage)
{
	JsonNode parameters;
	parameters["rewards"].Vector().resize(1);
	parameters["rewards"][0]["heroExperience"].Integer() = 0;
	if(percentage)
		parameters["rewards"][0]["heroExperienceNextLevelPercent"] = *percentage;

	Rewardable::Info info;
	info.init(parameters, "battleScholarRewardParserTest");
	Rewardable::Configuration configured;
	info.configureObject(configured, randomizer, callback);
	return configured.info.front().reward;
}
}

TEST_F(NewHorizonsBattleScholarAcademyTest, RealComputerOwnedVisitsPreviewGrantOncePerHeroAndPersist)
{
	const auto dungeon = FactionID(FactionID::decode("core:dungeon"));
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("BattleScholarAcademy")
		.playerActive(PlayerColor(0))
		.playerActive(PlayerColor(1))
		.town({18, 18, 0}, dungeon, PlayerColor(0))
		.town({28, 18, 0}, dungeon, PlayerColor(0))
		.hero({5, 5, 0}, heroType("core:christian"), PlayerColor(0))
		.hero({7, 5, 0}, heroType("core:valeska"), PlayerColor(0));
	startWithMap(std::move(builder));

	auto * firstTown = dynamic_cast<CGTownInstance *>(findObjectAt({18, 18, 0}));
	auto * secondTown = dynamic_cast<CGTownInstance *>(findObjectAt({28, 18, 0}));
	ASSERT_NE(firstTown, nullptr);
	ASSERT_NE(secondTown, nullptr);
	ASSERT_EQ(firstTown->getOwner(), PlayerColor(0));
	ASSERT_EQ(secondTown->getOwner(), PlayerColor(0));
	firstTown->addBuilding(BuildingID::SPECIAL_4);
	secondTown->addBuilding(BuildingID::SPECIAL_4);
	ASSERT_TRUE(firstTown->rewardableBuildings.contains(BuildingID::SPECIAL_4));
	ASSERT_TRUE(secondTown->rewardableBuildings.contains(BuildingID::SPECIAL_4));

	auto * firstHero = findHeroAt({5, 5, 0});
	auto * secondHero = findHeroAt({7, 5, 0});
	ASSERT_NE(firstHero, nullptr);
	ASSERT_NE(secondHero, nullptr);
	firstHero->level = 1;
	firstHero->setExperience(LIBRARY->heroh->reqExp(2) - 103, ChangeValueMode::ABSOLUTE);
	secondHero->level = 2;
	secondHero->setExperience(LIBRARY->heroh->reqExp(3) - 101, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(firstHero->level, 1u);
	ASSERT_EQ(secondHero->level, 2u);
	ASSERT_NE(firstHero->exp, secondHero->exp);
	EXPECT_FALSE(gameState()->getPlayerState(PlayerColor(0))->isHuman())
		<< "The authoritative visit path must also grant for a computer-owned hero";

	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler gameHandler(server, gameState());
	const auto & firstReward = firstTown->rewardableBuildings.at(BuildingID::SPECIAL_4)->configuration.info.front().reward;
	const auto & secondReward = secondTown->rewardableBuildings.at(BuildingID::SPECIAL_4)->configuration.info.front().reward;
	ASSERT_EQ(firstTown->rewardableBuildings.at(BuildingID::SPECIAL_4)->configuration.visitMode, Rewardable::VISIT_HERO);
	ASSERT_EQ(firstReward.heroExperience, 0);
	ASSERT_EQ(firstReward.heroExperienceNextLevelPercent, 25);
	ASSERT_EQ(secondReward.heroExperienceNextLevelPercent, 25);

	auto visit = [&](CGTownInstance * town, CGHeroInstance * hero)
	{
		const auto & reward = town->rewardableBuildings.at(BuildingID::SPECIAL_4)->configuration.info.front().reward;
		const auto before = hero->exp;
		const auto preview = experiencePreview(reward, hero);
		const auto expected = expectedPercentGrant(*hero, 25);
		EXPECT_EQ(preview, expected);
		town->setVisitingHero(hero);
		gameHandler.heroVisitCastle(town, hero);
		EXPECT_EQ(hero->exp - before, preview);
		EXPECT_TRUE(town->rewardableBuildings.at(BuildingID::SPECIAL_4)->wasVisited(hero));
	};

	visit(firstTown, firstHero);
	const auto afterFirstAward = firstHero->exp;
	EXPECT_GT(afterFirstAward, 0);

	// The same physical Academy cannot award this hero twice.
	firstTown->setVisitingHero(nullptr);
	firstTown->setVisitingHero(firstHero);
	gameHandler.heroVisitCastle(firstTown, firstHero);
	EXPECT_EQ(firstHero->exp, afterFirstAward);

	// A second Academy is a separate physical building and awards the hero again.
	firstTown->setVisitingHero(nullptr);
	visit(secondTown, firstHero);
	const auto afterSecondAcademy = firstHero->exp;
	EXPECT_GT(afterSecondAcademy, afterFirstAward);

	// The first physical Academy may independently train another hero.
	secondTown->setVisitingHero(nullptr);
	visit(firstTown, secondHero);
	const auto savedFirstTown = firstTown->id;
	const auto savedSecondTown = secondTown->id;
	const auto savedFirstHero = firstHero->id;
	const auto savedSecondHero = secondHero->id;
	const auto savedFirstExperience = firstHero->exp;
	const auto savedSecondExperience = secondHero->exp;

	const auto savedState = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(savedState);
	auto * restoredFirstHero = restored.getHero(savedFirstHero);
	auto * restoredSecondHero = restored.getHero(savedSecondHero);
	ASSERT_NE(restoredFirstHero, nullptr);
	ASSERT_NE(restoredSecondHero, nullptr);
	EXPECT_EQ(restoredFirstHero->exp, savedFirstExperience);
	EXPECT_EQ(restoredSecondHero->exp, savedSecondExperience);
	EXPECT_TRUE(restored.getTown(savedFirstTown)->rewardableBuildings.at(BuildingID::SPECIAL_4)->wasVisited(restoredFirstHero));
	EXPECT_TRUE(restored.getTown(savedFirstTown)->rewardableBuildings.at(BuildingID::SPECIAL_4)->wasVisited(restoredSecondHero));
	EXPECT_TRUE(restored.getTown(savedSecondTown)->rewardableBuildings.at(BuildingID::SPECIAL_4)->wasVisited(restoredFirstHero));
}

TEST_F(NewHorizonsBattleScholarAcademyTest, NextLevelExperienceFloorsAppliesLearningAndKeepsLegacySerialization)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, heroType("core:christian"), PlayerColor(0));
	startWithMap(std::move(builder));
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);

	const auto secondLevelExperience = LIBRARY->heroh->reqExp(2);
	ASSERT_GT(secondLevelExperience, 103);
	hero->setExperience(secondLevelExperience - 103, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(hero->level, 1);

	Reward reward;
	reward.heroExperience = 41;
	reward.heroExperienceNextLevelPercent = 25;
	const auto rawPercent = 25; // floor(103 * 25 / 100)
	const auto expectedWithoutLearning = hero->calculateXp(41) + hero->calculateXp(rawPercent);
	EXPECT_EQ(reward.calculateHeroExperience(hero), expectedWithoutLearning);
	EXPECT_EQ(experiencePreview(reward, hero), expectedWithoutLearning);

	const auto learning = SecondarySkill(SecondarySkill::decode("new-horizons:learning"));
	ASSERT_GE(learning.getNum(), 0);
	hero->setSecSkillLevel(learning, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	ASSERT_GT(hero->valOfBonuses(BonusType::HERO_EXPERIENCE_GAIN_PERCENT), 0);
	const auto expectedWithLearning = hero->calculateXp(41) + hero->calculateXp(rawPercent);
	EXPECT_EQ(reward.calculateHeroExperience(hero), expectedWithLearning);
	EXPECT_GT(expectedWithLearning, expectedWithoutLearning);

	Reward fixedOnly;
	fixedOnly.heroExperience = 41;
	EXPECT_EQ(fixedOnly.calculateHeroExperience(hero), hero->calculateXp(41));

	Reward negativeFixed;
	negativeFixed.heroExperience = -41;
	negativeFixed.heroExperienceNextLevelPercent = 25;
	EXPECT_EQ(negativeFixed.calculateHeroExperience(hero), hero->calculateXp(rawPercent));

	Reward atCap;
	atCap.heroExperienceNextLevelPercent = 25;
	gameState()->getMap().levelLimit = 1;
	EXPECT_EQ(atCap.calculateHeroExperience(hero), 0);

	GameRandomizer randomizer(*gameState());
	auto parsePercent = [&](const std::optional<JsonNode> & value)
	{
		return parseConfiguredReward(gameState().get(), randomizer, value).heroExperienceNextLevelPercent;
	};
	JsonNode validPercent;
	validPercent.Integer() = 25;
	EXPECT_EQ(parsePercent(validPercent), 25);
	EXPECT_EQ(parsePercent(std::nullopt), 0);
	for(const int invalid : {-1, 101})
	{
		JsonNode node;
		node.Integer() = invalid;
		EXPECT_THROW(parsePercent(node), std::runtime_error);
	}
	JsonNode fractionalPercent;
	fractionalPercent.Float() = 25.0;
	EXPECT_THROW(parsePercent(fractionalPercent), std::runtime_error);

	Reward currentReward;
	currentReward.heroExperience = 41;
	currentReward.heroExperienceNextLevelPercent = 25;
	CMemorySerializer currentWriter;
	currentWriter.oser.version = ESerializationVersion::CURRENT;
	currentWriter.oser & currentReward;
	const auto currentBytes = currentWriter.extractBuffer();
	CMemorySerializer currentReader(currentBytes);
	currentReader.iser.version = ESerializationVersion::CURRENT;
	Reward restoredCurrentReward;
	currentReader.iser & restoredCurrentReward;
	EXPECT_EQ(restoredCurrentReward.heroExperience, 41);
	EXPECT_EQ(restoredCurrentReward.heroExperienceNextLevelPercent, 25);

	// Old saves are produced with the old writer. Loading those bytes must reset
	// the new field and leave the following serialized value aligned.
	const auto oldVersion = ESerializationVersion::NEW_HORIZONS_CREATURE_ABILITY_SUPPRESSION;
	Reward oldReward;
	CMemorySerializer oldWriter;
	oldWriter.oser.version = oldVersion;
	oldWriter.oser & oldReward;
	const uint32_t sentinel = 0x4198bca2;
	oldWriter.oser & sentinel;
	const auto oldBytes = oldWriter.extractBuffer();

	CMemorySerializer oldReader(oldBytes);
	oldReader.iser.version = oldVersion;
	Reward restoredOldReward;
	restoredOldReward.heroExperienceNextLevelPercent = 77;
	oldReader.iser & restoredOldReward;
	uint32_t restoredSentinel = 0;
	oldReader.iser & restoredSentinel;
	EXPECT_EQ(restoredOldReward.heroExperienceNextLevelPercent, 0);
	EXPECT_EQ(restoredSentinel, sentinel);

	CMemorySerializer rejectedDownsave;
	rejectedDownsave.oser.version = oldVersion;
	Reward unsupportedReward;
	unsupportedReward.heroExperienceNextLevelPercent = 25;
	EXPECT_THROW(rejectedDownsave.oser & unsupportedReward, std::runtime_error);
	EXPECT_TRUE(rejectedDownsave.extractBuffer().empty());
}
