/*
 * NewHorizonsHistorianRewardTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"

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
#include <stdexcept>

namespace
{
constexpr PlayerColor PLAYER(0);
constexpr auto LEARNING_SKILL = "new-horizons:learning";
constexpr auto HISTORIAN_PERK = "new-horizons:learning.historian";

class NewHorizonsHistorianRewardTest : public TinyMapGameTest
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

		// Historian is activated only in this saved-world fixture. Production
		// registration remains planned; no other perk definition is changed.
		auto perkRules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		for(auto & perk : perkRules["skills"][LEARNING_SKILL]["perks"].Vector())
			if(perk["id"].String() == HISTORIAN_PERK)
				perk["effect"]["status"].String() = "active";
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
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
	auto * stone = dynamic_cast<CRewardableObject *>(stoneObject.get());
	ASSERT_NE(stone, nullptr);
	ASSERT_FALSE(stone->configuration.info.empty());
	ASSERT_EQ(stone->configuration.visitMode, Rewardable::VISIT_HERO);
	const auto stoneReward = stone->configuration.info.front().reward;
	ASSERT_EQ(stoneReward.heroExperience, 1000);
	ASSERT_TRUE(stoneReward.primaryExperienceReward);
	ASSERT_EQ(stoneReward.heroExperienceNextLevelPercent, 0);
	gameHandler.newObject(stoneObject, PLAYER);

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

	// An active Historian does not promote an unclassified fixed-XP reward or
	// a next-level percentage reward into the primary fixed-XP category.
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
	EXPECT_EQ(percentage.calculateHeroExperience(historian), historian->calculateXp(rawPercent));
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
