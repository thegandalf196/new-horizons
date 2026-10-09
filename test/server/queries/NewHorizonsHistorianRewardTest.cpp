/*
 * NewHorizonsHistorianRewardTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"

#include "AI/Nullkiller2/Engine/PriorityEvaluator.h"

#include "../../mock/GameHandlerTestServer.h"
#include "../../mock/TinyH3MBuilder.h"
#include "../../mock/TinyMapGameTest.h"

#include "../../../lib/CSkillHandler.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CRewardableObject.h"
#include "../../../lib/mapObjectConstructors/CObjectClassesHandler.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/Component.h"
#include "../../../lib/rewardable/Reward.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../lib/serializer/JsonDeserializer.h"
#include "../../../lib/serializer/JsonSerializer.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/queries/CQuery.h"
#include "../../../server/queries/QueriesProcessor.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
constexpr PlayerColor PLAYER(0);
constexpr auto LEARNING_SKILL = "new-horizons:learning";
constexpr auto HISTORIAN_PERK = "new-horizons:learning.historian";

class NewHorizonsHistorianRewardTest : public TinyMapGameTest
{
protected:
	bool legacyRules = false;
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		if(legacyRules)
		{
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode());
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode());
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode());
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
			return;
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));

		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	static HeroTypeID heroType(const char * id)
	{
		return HeroTypeID(HeroTypeID::decode(id));
	}

	static TExpType experiencePreview(const Rewardable::Reward & reward, const CGHeroInstance * hero)
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

	void selectBasicLearning(CGHeroInstance * hero, bool selectHistorian)
	{
		const auto learning = SecondarySkill(SecondarySkill::decode(LEARNING_SKILL));
		ASSERT_NE(learning, SecondarySkill(SecondarySkill::NONE));
		hero->setSecSkillLevel(learning, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		if(selectHistorian)
			hero->applyPerkSelection({LEARNING_SKILL, HISTORIAN_PERK});
		EXPECT_EQ(hero->getSecSkillLevel(learning), MasteryLevel::BASIC);
		EXPECT_EQ(hero->hasActivePerk(LEARNING_SKILL, HISTORIAN_PERK), selectHistorian);
	}
};
}

TEST_F(NewHorizonsHistorianRewardTest, LearningStonePreviewAndAcceptedVisitUsePrimaryRewardAndLearningTogether)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PLAYER)
		.hero({5, 5, 0}, heroType("core:christian"), PLAYER)
		.hero({7, 5, 0}, heroType("core:tyris"), PLAYER);
	startWithMap(std::move(builder));

	auto * historian = findHeroAt({5, 5, 0});
	auto * noHistorian = findHeroAt({7, 5, 0});
	ASSERT_NE(historian, nullptr);
	ASSERT_NE(noHistorian, nullptr);
	selectBasicLearning(historian, true);
	selectBasicLearning(noHistorian, false);
	EXPECT_GT(historian->valOfBonuses(BonusType::HERO_EXPERIENCE_GAIN_PERCENT), 0);

	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler gameHandler(server, gameState());
	gameHandler.onAdvInterfaceReady(PLAYER);
	auto resolveLevelUpQueries = [&]() -> bool
	{
		const auto maximumSupportedLevel = LIBRARY->heroh->maxSupportedLevel();
		for(ui32 resolved = 0; resolved < maximumSupportedLevel; ++resolved)
		{
			const auto query = gameHandler.queries->topQuery(PLAYER);
			if(!query)
				return true;

			if(query->getType() != QueryType::HeroLevelUpDialog)
			{
				ADD_FAILURE() << "Unexpected query while resolving Learning Stone level-ups: "
					<< static_cast<int>(query->getType());
				return false;
			}
			if(!gameHandler.queryReply(query->queryID, 0, PLAYER))
			{
				ADD_FAILURE() << "Could not accept the first valid level-up choice";
				return false;
			}
		}
		ADD_FAILURE() << "Level-up queries exceeded the maximum supported hero level";
		return false;
	};
	auto stoneObject = gameHandler.createNewObject({12, 12, 0}, Obj::LEARNING_STONE, MapObjectSubID(0));
	ASSERT_NE(stoneObject, nullptr);
	gameHandler.newObject(stoneObject, PLAYER);
	const auto stoneId = stoneObject->id;
	auto * stone = dynamic_cast<CRewardableObject *>(gameState()->getObjInstance(stoneId));
	ASSERT_NE(stone, nullptr);
	ASSERT_FALSE(stone->configuration.info.empty());
	ASSERT_EQ(stone->configuration.visitMode, Rewardable::VISIT_HERO);
	const auto stoneReward = stone->configuration.info.front().reward;
	ASSERT_EQ(stoneReward.heroExperience, 1000);
	ASSERT_TRUE(stoneReward.primaryExperienceReward);
	ASSERT_EQ(stoneReward.heroExperienceNextLevelPercent, 0);

	const auto historianOrdinaryXp = historian->calculateXp(stoneReward.heroExperience);
	const auto historianExpectedXp = historian->calculateXp(stoneReward.heroExperience, 50);
	ASSERT_GT(historianExpectedXp, historianOrdinaryXp);
	EXPECT_EQ(stoneReward.calculateHeroExperience(historian), historianExpectedXp);
	EXPECT_EQ(experiencePreview(stoneReward, historian), historianExpectedXp);

	const auto historianExperienceBefore = historian->exp;
	gameHandler.objectVisited(stone, historian);
	EXPECT_EQ(historian->exp - historianExperienceBefore, historianExpectedXp);
	EXPECT_TRUE(stone->wasVisited(historian));
	if(!resolveLevelUpQueries())
		return;
	ASSERT_EQ(gameHandler.getVisitingHero(stone), nullptr);

	// The same marked object does not grant the bonus to a visitor who lacks
	// Historian; their ordinary Learning reward still uses calculateXp once.
	const auto ordinaryExpectedXp = noHistorian->calculateXp(stoneReward.heroExperience);
	EXPECT_EQ(stoneReward.calculateHeroExperience(noHistorian), ordinaryExpectedXp);
	EXPECT_EQ(experiencePreview(stoneReward, noHistorian), ordinaryExpectedXp);
	const auto noHistorianExperienceBefore = noHistorian->exp;
	gameHandler.objectVisited(stone, noHistorian);
	EXPECT_EQ(noHistorian->exp - noHistorianExperienceBefore, ordinaryExpectedXp);
	EXPECT_TRUE(stone->wasVisited(noHistorian));
	if(!resolveLevelUpQueries())
		return;
	ASSERT_EQ(gameHandler.getVisitingHero(stone), nullptr);

	// Only explicitly classified primary Experience receives Historian.
	const auto currentHistorianOrdinaryXp = historian->calculateXp(stoneReward.heroExperience);
	Rewardable::Reward unclassified;
	unclassified.heroExperience = stoneReward.heroExperience;
	EXPECT_FALSE(unclassified.primaryExperienceReward);
	EXPECT_EQ(unclassified.calculateHeroExperience(historian), currentHistorianOrdinaryXp);

	Rewardable::Reward percentage;
	percentage.heroExperienceNextLevelPercent = 25;
	percentage.primaryExperienceReward = true;
	const auto levelGap = LIBRARY->heroh->reqExp(historian->level + 1) - historian->exp;
	const auto rawPercent = (levelGap / 100) * 25 + (levelGap % 100) * 25 / 100;
	EXPECT_EQ(percentage.calculateHeroExperience(historian), historian->calculateXp(rawPercent, 50));
	EXPECT_EQ(percentage.calculateHeroExperience(historian, false), historian->calculateXp(rawPercent));
	percentage.primaryExperienceReward = false;
	EXPECT_EQ(percentage.calculateHeroExperience(historian), historian->calculateXp(rawPercent));
}

TEST_F(NewHorizonsHistorianRewardTest, LearningStoneSkillScoreUsesClassifiedRewardRatioAndKeepsBaselineControls)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PLAYER)
		.hero({5, 5, 0}, heroType("core:christian"), PLAYER)
		.hero({7, 5, 0}, heroType("core:tyris"), PLAYER);
	startWithMap(std::move(builder));

	auto * historian = findHeroAt({5, 5, 0});
	auto * noHistorian = findHeroAt({7, 5, 0});
	ASSERT_NE(historian, nullptr);
	ASSERT_NE(noHistorian, nullptr);
	selectBasicLearning(historian, true);
	selectBasicLearning(noHistorian, false);

	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler gameHandler(server, gameState());
	auto stoneObject = gameHandler.createNewObject({12, 12, 0}, Obj::LEARNING_STONE, MapObjectSubID(0));
	ASSERT_NE(stoneObject, nullptr);
	gameHandler.newObject(stoneObject, PLAYER);
	const auto stoneId = stoneObject->id;
	auto * stone = dynamic_cast<CRewardableObject *>(gameState()->getObjInstance(stoneId));
	ASSERT_NE(stone, nullptr);
	ASSERT_FALSE(stone->configuration.info.empty());
	ASSERT_EQ(stone->configuration.visitMode, Rewardable::VISIT_HERO);
	ASSERT_EQ(stone->configuration.info.front().visitType, Rewardable::EEventType::EVENT_FIRST_VISIT);
	const auto actualReward = stone->configuration.info.front().reward;
	ASSERT_TRUE(actualReward.primaryExperienceReward);
	ASSERT_GT(actualReward.heroExperience, 0);
	ASSERT_EQ(stone->getAvailableRewards(historian, Rewardable::EEventType::EVENT_FIRST_VISIT),
		(std::vector<ui32>{0}));

	// The Stone-specific scorer is a pure forecast branch and does not need an
	// initialized AI instance; other target types keep their existing AI context.
	NK2AI::RewardEvaluator evaluator(nullptr);
	const auto baselinePriority = [](const CGHeroInstance * hero)
	{
		return 1.0f / std::sqrt(static_cast<float>(hero->level));
	};
	auto ordinaryReward = actualReward;
	ordinaryReward.primaryExperienceReward = false;
	const auto ordinaryExperience = ordinaryReward.calculateHeroExperience(historian);
	const auto actualExperience = actualReward.calculateHeroExperience(historian);
	ASSERT_GT(ordinaryExperience, 0);
	ASSERT_GT(actualExperience, ordinaryExperience);
	const auto expectedHistorianPriority = baselinePriority(historian)
		* static_cast<float>(actualExperience) / static_cast<float>(ordinaryExperience);

	const auto historianExperienceBefore = historian->exp;
	const auto ordinaryExperienceBefore = noHistorian->exp;
	EXPECT_NEAR(evaluator.getSkillReward(stone, historian, NK2AI::HeroRole::MAIN), expectedHistorianPriority, 1e-6f);
	EXPECT_NEAR(evaluator.getSkillReward(stone, noHistorian, NK2AI::HeroRole::MAIN), baselinePriority(noHistorian), 1e-6f);

	// An unclassified reward and a reward row unavailable for a first visit keep
	// the original fixed Learning Stone priority.
	auto & stoneInfo = stone->configuration.info.front();
	const bool originalClassification = stoneInfo.reward.primaryExperienceReward;
	stoneInfo.reward.primaryExperienceReward = false;
	EXPECT_NEAR(evaluator.getSkillReward(stone, historian, NK2AI::HeroRole::MAIN), baselinePriority(historian), 1e-6f);
	stoneInfo.reward.primaryExperienceReward = originalClassification;

	const auto originalVisitType = stoneInfo.visitType;
	stoneInfo.visitType = Rewardable::EEventType::EVENT_ALREADY_VISITED;
	EXPECT_TRUE(stone->getAvailableRewards(historian, Rewardable::EEventType::EVENT_FIRST_VISIT).empty());
	EXPECT_NEAR(evaluator.getSkillReward(stone, historian, NK2AI::HeroRole::MAIN), baselinePriority(historian), 1e-6f);
	stoneInfo.visitType = originalVisitType;

	const auto originalFixedExperience = stoneInfo.reward.heroExperience;
	stoneInfo.reward.heroExperience = 0;
	EXPECT_NEAR(evaluator.getSkillReward(stone, historian, NK2AI::HeroRole::MAIN), baselinePriority(historian), 1e-6f);
	stoneInfo.reward.heroExperience = originalFixedExperience;

	EXPECT_EQ(historian->exp, historianExperienceBefore);
	EXPECT_EQ(noHistorian->exp, ordinaryExperienceBefore);
	EXPECT_FALSE(stone->wasVisited(historian));
	EXPECT_TRUE(stoneInfo.reward.primaryExperienceReward);
	EXPECT_EQ(stoneInfo.reward.heroExperience, originalFixedExperience);
	EXPECT_EQ(stoneInfo.visitType, originalVisitType);
}

TEST_F(NewHorizonsHistorianRewardTest, TreePreviewAcceptedRewardAndAIUseTheSamePrimaryExperience)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PLAYER)
		.hero({5, 5, 0}, heroType("core:christian"), PLAYER);
	startWithMap(std::move(builder));
	auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	selectBasicLearning(hero, true);
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler gameHandler(server, gameState());
	gameHandler.onAdvInterfaceReady(PLAYER);
	gameHandler.giveResource(PLAYER, GameResID(GameResID::GOLD), 10000);
	gameHandler.giveResource(PLAYER, GameResID(GameResID::GEMS), 100);
	auto object = gameHandler.createNewObject({12, 12, 0}, Obj::TREE_OF_KNOWLEDGE, MapObjectSubID(0));
	ASSERT_NE(object, nullptr);
	gameHandler.newObject(object, PLAYER);
	auto * tree = dynamic_cast<CRewardableObject *>(gameState()->getObjInstance(object->id));
	ASSERT_NE(tree, nullptr);
	const auto available = tree->getAvailableRewards(hero, Rewardable::EEventType::EVENT_FIRST_VISIT);
	ASSERT_EQ(available.size(), 1);
	const auto reward = tree->configuration.info.at(available.front()).reward;
	ASSERT_EQ(reward.heroLevel, 1);
	ASSERT_TRUE(reward.primaryExperienceReward);
	const TExpType base = LIBRARY->heroh->reqExp(hero->level + 1) - LIBRARY->heroh->reqExp(hero->level);
	const auto expected = hero->calculateXp(base, 50);
	EXPECT_EQ(reward.calculateHeroExperience(hero), expected);
	EXPECT_EQ(reward.calculateHeroExperience(hero, false), hero->calculateXp(base));
	EXPECT_EQ(experiencePreview(reward, hero), expected);
	NK2AI::RewardEvaluator evaluator(nullptr);
	EXPECT_NEAR(evaluator.getSkillReward(tree, hero, NK2AI::HeroRole::MAIN),
		static_cast<float>(expected) / hero->calculateXp(base), 1e-6f);
	const auto before = hero->exp;
	gameHandler.objectVisited(tree, hero);
	const auto confirmation = gameHandler.queries->topQuery(PLAYER);
	ASSERT_NE(confirmation, nullptr);
	ASSERT_EQ(confirmation->getType(), QueryType::BlockingDialog);
	ASSERT_TRUE(gameHandler.queryReply(confirmation->queryID, 1, PLAYER));
	EXPECT_EQ(hero->exp - before, expected);
	EXPECT_TRUE(tree->wasVisited(hero));
	for(ui32 i = 0; i < LIBRARY->heroh->maxSupportedLevel(); ++i)
	{
		const auto query = gameHandler.queries->topQuery(PLAYER);
		if(!query)
			break;
		ASSERT_EQ(query->getType(), QueryType::HeroLevelUpDialog);
		ASSERT_TRUE(gameHandler.queryReply(query->queryID, 0, PLAYER));
	}
	EXPECT_EQ(gameHandler.queries->topQuery(PLAYER), nullptr);
	EXPECT_EQ(gameHandler.getVisitingHero(tree), nullptr);
}

TEST_F(NewHorizonsHistorianRewardTest, PrimaryLevelRewardPreservesLegacyDeltaAndUsesLearningWithoutHistorian)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PLAYER)
		.hero({5, 5, 0}, heroType("core:christian"), PLAYER);
	startWithMap(std::move(builder));
	auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	selectBasicLearning(hero, false);
	Rewardable::Reward reward;
	reward.heroLevel = 1;
	const TExpType base = LIBRARY->heroh->reqExp(hero->level + 1) - LIBRARY->heroh->reqExp(hero->level);
	EXPECT_EQ(reward.calculateHeroExperience(hero), base);
	reward.primaryExperienceReward = true;
	EXPECT_EQ(reward.calculateHeroExperience(hero), hero->calculateXp(base));
	EXPECT_EQ(experiencePreview(reward, hero), hero->calculateXp(base));
	hero->applyPerkSelection({LEARNING_SKILL, HISTORIAN_PERK});
	EXPECT_EQ(reward.calculateHeroExperience(hero), hero->calculateXp(base, 50));
	EXPECT_EQ(reward.calculateHeroExperience(hero, false), hero->calculateXp(base));
	// Tree preserves the existing level-threshold delta, not the visitor's gap.
	++hero->exp;
	EXPECT_EQ(reward.calculateHeroExperience(hero), hero->calculateXp(base, 50));
	hero->level = LIBRARY->heroh->maxSupportedLevel();
	EXPECT_EQ(reward.calculateHeroExperience(hero), 0);
	EXPECT_EQ(experiencePreview(reward, hero), 0);
}

TEST_F(NewHorizonsHistorianRewardTest, FreshClassifiedTreeKeepsLegacyLevelGrantPreviewAndAIPriority)
{
	legacyRules = true;
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PLAYER)
		.hero({5, 5, 0}, heroType("core:christian"), PLAYER);
	startWithMap(std::move(builder));
	auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	hero->setSecSkillLevel(SecondarySkill(SecondarySkill::LEARNING), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler gameHandler(server, gameState());
	gameHandler.onAdvInterfaceReady(PLAYER);
	gameHandler.giveResource(PLAYER, GameResID(GameResID::GOLD), 10000);
	gameHandler.giveResource(PLAYER, GameResID(GameResID::GEMS), 100);
	auto object = gameHandler.createNewObject({12, 12, 0}, Obj::TREE_OF_KNOWLEDGE, MapObjectSubID(0));
	ASSERT_NE(object, nullptr);
	gameHandler.newObject(object, PLAYER);
	auto * tree = dynamic_cast<CRewardableObject *>(gameState()->getObjInstance(object->id));
	ASSERT_NE(tree, nullptr);
	const auto available = tree->getAvailableRewards(hero, Rewardable::EEventType::EVENT_FIRST_VISIT);
	ASSERT_EQ(available.size(), 1);
	const auto reward = tree->configuration.info.at(available.front()).reward;
	ASSERT_TRUE(reward.primaryExperienceReward);
	ASSERT_EQ(reward.heroLevel, 1);
	const TExpType base = LIBRARY->heroh->reqExp(hero->level + 1) - LIBRARY->heroh->reqExp(hero->level);
	ASSERT_GT(hero->calculateXp(base), base);
	EXPECT_EQ(reward.calculateHeroExperience(hero), base);
	EXPECT_EQ(reward.calculateHeroExperience(hero, false), base);
	EXPECT_EQ(experiencePreview(reward, hero), 0);
	std::vector<Component> components;
	reward.loadComponents(components, hero);
	EXPECT_TRUE(std::ranges::any_of(components, [](const Component & component)
	{
		return component.type == ComponentType::LEVEL && component.value == 1;
	}));
	NK2AI::RewardEvaluator evaluator(nullptr);
	EXPECT_FLOAT_EQ(evaluator.getSkillReward(tree, hero, NK2AI::HeroRole::MAIN), 1.0f);
	const auto before = hero->exp;
	gameHandler.objectVisited(tree, hero);
	const auto confirmation = gameHandler.queries->topQuery(PLAYER);
	ASSERT_NE(confirmation, nullptr);
	ASSERT_EQ(confirmation->getType(), QueryType::BlockingDialog);
	ASSERT_TRUE(gameHandler.queryReply(confirmation->queryID, 1, PLAYER));
	EXPECT_EQ(hero->exp - before, base);
	for(ui32 i = 0; i < LIBRARY->heroh->maxSupportedLevel(); ++i)
	{
		const auto query = gameHandler.queries->topQuery(PLAYER);
		if(!query)
			break;
		ASSERT_EQ(query->getType(), QueryType::HeroLevelUpDialog);
		ASSERT_TRUE(gameHandler.queryReply(query->queryID, 0, PLAYER));
	}
	EXPECT_EQ(gameHandler.queries->topQuery(PLAYER), nullptr);
}

TEST_F(NewHorizonsHistorianRewardTest, ShippedChestAndTreeClassifyOnlyExperienceOptions)
{
	const JsonNode pickable(JsonPath::builtin("config/objects/rewardablePickable"));
	const auto & chest = pickable["treasureChest"]["types"]["treasureChest"]["rewards"].Vector();
	size_t experienceOptions = 0;
	for(const auto & row : chest)
	{
		if(row["heroExperience"].Integer() > 0)
		{
			++experienceOptions;
			EXPECT_TRUE(row["primaryExperienceReward"].Bool());
		}
		else
			EXPECT_FALSE(row["primaryExperienceReward"].Bool());
	}
	EXPECT_EQ(experienceOptions, 3);
	const JsonNode once(JsonPath::builtin("config/objects/rewardableOncePerHero"));
	const auto & tree = once["treeOfKnowledge"]["types"]["treeOfKnowledge"]["rewards"].Vector();
	ASSERT_EQ(tree.size(), 3);
	for(const auto & row : tree)
	{
		EXPECT_EQ(row["heroLevel"].Integer(), 1);
		EXPECT_TRUE(row["primaryExperienceReward"].Bool());
	}
}

TEST_F(NewHorizonsHistorianRewardTest, PrimaryRewardClassificationHasJSONAndVersionedBinaryCompatibility)
{
	Rewardable::Reward classified;
	classified.heroExperience = 1000;
	classified.primaryExperienceReward = true;

	JsonNode json;
	JsonSerializer jsonWriter(nullptr, json);
	classified.serializeJson(jsonWriter);
	ASSERT_TRUE(json["primaryExperienceReward"].isBool());
	EXPECT_TRUE(json["primaryExperienceReward"].Bool());
	Rewardable::Reward jsonRestored;
	JsonDeserializer jsonReader(nullptr, json);
	jsonRestored.serializeJson(jsonReader);
	EXPECT_TRUE(jsonRestored.primaryExperienceReward);

	Rewardable::Reward defaultReward;
	EXPECT_FALSE(defaultReward.primaryExperienceReward);
	JsonNode defaultJson;
	JsonSerializer defaultWriter(nullptr, defaultJson);
	defaultReward.serializeJson(defaultWriter);
	EXPECT_TRUE(defaultJson["primaryExperienceReward"].isNull());
	defaultJson.Struct().erase("primaryExperienceReward");
	Rewardable::Reward legacyJsonRestored;
	legacyJsonRestored.primaryExperienceReward = true;
	JsonDeserializer legacyJsonReader(nullptr, defaultJson);
	legacyJsonRestored.serializeJson(legacyJsonReader);
	EXPECT_FALSE(legacyJsonRestored.primaryExperienceReward);

	JsonNode malformedJson = json;
	malformedJson["primaryExperienceReward"].String() = "true";
	Rewardable::Reward malformedRestored;
	JsonDeserializer malformedReader(nullptr, malformedJson);
	EXPECT_THROW(malformedRestored.serializeJson(malformedReader), std::runtime_error);

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	current.oser & classified;
	Rewardable::Reward currentRestored;
	current.iser & currentRestored;
	EXPECT_TRUE(currentRestored.primaryExperienceReward);
	EXPECT_EQ(currentRestored.heroExperience, classified.heroExperience);

	const auto oldVersion = ESerializationVersion::NEW_HORIZONS_REWARDABLE_NEXT_LEVEL_EXPERIENCE;
	CMemorySerializer oldWriter;
	oldWriter.oser.version = oldVersion;
	Rewardable::Reward unsupported;
	unsupported.primaryExperienceReward = true;
	EXPECT_THROW(oldWriter.oser & unsupported, std::runtime_error);
	EXPECT_TRUE(oldWriter.extractBuffer().empty());

	Rewardable::Reward legacyReward;
	CMemorySerializer legacyWriter;
	legacyWriter.oser.version = oldVersion;
	legacyWriter.iser.version = oldVersion;
	legacyWriter.oser & legacyReward;
	const uint32_t sentinel = 0x4e485237;
	legacyWriter.oser & sentinel;
	Rewardable::Reward legacyRestored;
	legacyRestored.primaryExperienceReward = true;
	legacyWriter.iser & legacyRestored;
	uint32_t restoredSentinel = 0;
	legacyWriter.iser & restoredSentinel;
	EXPECT_FALSE(legacyRestored.primaryExperienceReward);
	EXPECT_EQ(restoredSentinel, sentinel);
}
