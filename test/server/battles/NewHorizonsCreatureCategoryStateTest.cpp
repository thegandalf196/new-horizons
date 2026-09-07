/*
 * NewHorizonsCreatureCategoryStateTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/campaign/CampaignState.h"
#include "../../../lib/gameState/TavernHeroesPool.h"
#include "../../../lib/gameState/CGameStateCampaign.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/entities/building/TownFortifications.h"
#include "../../../lib/rmg/CMapGenOptions.h"
#include "../../../lib/mapObjects/MiscObjects.h"
#include "../../../lib/mapObjects/ObjectTemplate.h"
#include "../../../lib/mapObjects/TownBuildingInstance.h"
#include "../../../lib/mapping/CCastleEvent.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include <limits>
#include <stdexcept>

using namespace newHorizonsCreatures;

namespace
{
JsonNode contextFixture()
{
	// Partial synthetic assignment table, NOT an activated or approved roster.
	JsonNode rules;
	rules["schemaVersion"].Integer() = 1;
	rules["rulesetVersion"].Integer() = CREATURE_CATEGORY_RULESET_VERSION;
	rules["sourceRulesetId"].String() = "new-horizons:creatureCategories";
	for(const std::string name : {"core", "elite", "champion"})
	{
		rules["categories"][name]["nameTextId"].String() = "new-horizons.category." + name + ".name";
		rules["categories"][name]["descriptionTextId"].String() = "new-horizons.category." + name + ".description";
	}
	rules["creatures"]["core:pixie"].String() = "core";
	rules["creatures"]["core:airElemental"].String() = "elite";
	rules["creatures"]["core:phoenix"].String() = "champion";
	return rules;
}

CreatureID pixie()
{
	return CreatureID(CreatureID::decode("core:pixie"));
}

// Adversarial world callback: actual world access delegates normally, but its
// captured classification deliberately disagrees with the incoming battle.
class CategoryWorldCallback final : public CGameInfoCallback
{
	CGameState & state;
	CreatureCategoryRules rules;
public:
	CategoryWorldCallback(CGameState & state, const JsonNode & rules)
		: state(state), rules(captureCreatureCategoryRules(rules)) {}
	CGameState & gameState() override { return state; }
	const CGameState & gameState() const override { return state; }
	const CreatureCategoryRules & getCreatureCategoryRules() const override { return rules; }
};

class InstalledCategoryOverride
{
	std::unique_ptr<GameSettings> previous;
public:
	explicit InstalledCategoryOverride(const JsonNode & rules)
	{
		auto config = LIBRARY->settingsHandler->getFullConfig();
		config["creatures"]["newHorizonsCategories"] = rules;
		auto replacement = std::make_unique<GameSettings>();
		replacement->loadBase(config);
		previous = std::move(LIBRARY->settingsHandler);
		LIBRARY->settingsHandler = std::move(replacement);
	}
	~InstalledCategoryOverride() { LIBRARY->settingsHandler = std::move(previous); }
};

class CategoryEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
	const GameCb * world;
public:
	explicit CategoryEnvironment(std::shared_ptr<CGameState> state, const GameCb * world = nullptr)
		: state(std::move(state)), world(world) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return world ? world : state.get(); }
};
}

class NewHorizonsCreatureCategoryStateTest : public HeroCommandFixture
{
protected:
	bool enabled = true;
	JsonNode authoredRules = contextFixture();
	void mapLoaded(CMap * map) override
	{
		useCommands = false;
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::CREATURES_NEW_HORIZONS_CATEGORIES,
			enabled ? authoredRules : JsonNode());
	}
};

TEST_F(NewHorizonsCreatureCategoryStateTest, ActualWorldUsesCanonicalRowsNotTierUpgradeOrInvalidIds)
{
	// Map overrides deep-merge with installed rules. Isolate this deliberately
	// partial table before world initialization; otherwise a real installed Sprite
	// row survives the merge and cannot serve as an unmapped-upgrade control.
	InstalledCategoryOverride installed(authoredRules);
	startGame();
	ASSERT_EQ(gameState()->getCreatureCategoryRules().getRules(), authoredRules);
	ASSERT_EQ(gameState()->getCreatureCategoryRules().getRules()["creatures"].Struct().size(), 3u);
	const auto view = gameState()->getCreatureCategory(pixie());
	ASSERT_TRUE(view);
	EXPECT_EQ(view->category, CreatureCategory::CORE);
	const CreatureID sprite(CreatureID::decode("core:sprite"));
	ASSERT_NE(sprite, CreatureID::NONE);
	EXPECT_FALSE(gameState()->getCreatureCategory(sprite));
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	ASSERT_NE(pikeman, CreatureID::NONE);
	EXPECT_FALSE(gameState()->getCreatureCategory(pikeman));
	EXPECT_FALSE(gameState()->getCreatureCategory(CreatureID::NONE));
	EXPECT_FALSE(gameState()->getCreatureCategory(CreatureID(std::numeric_limits<int32_t>::max())));
	auto invalid = contextFixture();
	invalid["creatures"]["core:missingCategoryCreature"].String() = "core";
	EXPECT_THROW(captureCreatureCategoryRules(invalid), std::runtime_error);
	// This implicit-scope spelling really resolves, but is not a canonical row.
	ASSERT_EQ(CreatureID::decode("pixie"), pixie().getNum());
	invalid = contextFixture();
	invalid["creatures"]["pixie"].String() = "champion";
	EXPECT_THROW(captureCreatureCategoryRules(invalid), std::runtime_error);
	EXPECT_EQ(gameState()->getCreatureCategory(pixie()), view);
}

TEST_F(NewHorizonsCreatureCategoryStateTest, InvalidWorldTableCannotPublishItsOtherwiseValidRows)
{
	authoredRules["creatures"]["core:pixie"].String() = "champion";
	authoredRules["creatures"]["core:missingCategoryCreature"].String() = "elite";
	try
	{
		startGame();
		FAIL() << "Expected category entity validation to reject the world";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_EQ(std::string(error.what()),
			"Unknown or noncanonical creature category identifier: core:missingCategoryCreature");
	}
	EXPECT_FALSE(gameState()->getCreatureCategory(pixie()));
}

TEST_F(NewHorizonsCreatureCategoryStateTest, CapturedWorldAndBattleIgnoreLaterSettingsChanges)
{
	startGame();
	auto replacement = contextFixture();
	replacement["creatures"]["core:pixie"].String() = "champion";
	replacement["sourceRulesetId"].String() = "new-horizons:changedInstalledCategories";
	InstalledCategoryOverride installed(replacement);
	ASSERT_EQ(LIBRARY->engineSettings()->getValue(EGameSettings::CREATURES_NEW_HORIZONS_CATEGORIES), replacement);
	map()->overrideGameSetting(EGameSettings::CREATURES_NEW_HORIZONS_CATEGORIES, replacement);
	ASSERT_TRUE(gameState()->getCreatureCategory(pixie()));
	EXPECT_EQ(gameState()->getCreatureCategory(pixie())->category, CreatureCategory::CORE);
	startBattle();
	ASSERT_TRUE(battle()->battleGetCreatureCategory(pixie()));
	EXPECT_EQ(battle()->battleGetCreatureCategory(pixie())->category, CreatureCategory::CORE);
	EXPECT_EQ(battle()->battleGetCreatureCategory(pixie())->sourceRulesetId, "new-horizons:creatureCategories");
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	ASSERT_NE(pikeman, CreatureID::NONE);
	EXPECT_FALSE(battle()->battleGetCreatureCategory(pikeman));
}

TEST_F(NewHorizonsCreatureCategoryStateTest, AbsentWorldNeverAdoptsLaterSettings)
{
	enabled = false;
	startGame();
	InstalledCategoryOverride installed(contextFixture());
	map()->overrideGameSetting(EGameSettings::CREATURES_NEW_HORIZONS_CATEGORIES, contextFixture());
	EXPECT_FALSE(gameState()->getCreatureCategory(pixie()));
	startBattle();
	EXPECT_FALSE(battle()->battleGetCreatureCategory(pixie()));
	const auto bytes = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(bytes);
	EXPECT_FALSE(restored.getCreatureCategory(pixie()));
}

TEST_F(NewHorizonsCreatureCategoryStateTest, ActualWorldSaveRetainsNamedCopiedView)
{
	startGame();
	const auto original = gameState()->getCreatureCategory(pixie());
	ASSERT_TRUE(original);
	const auto bytes = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(bytes);
	EXPECT_EQ(restored.getCreatureCategory(pixie()), original);
	auto copy = restored.getCreatureCategory(pixie());
	copy->nameTextId = "changed locally";
	EXPECT_EQ(restored.getCreatureCategory(pixie()), original);
}

TEST_F(NewHorizonsCreatureCategoryStateTest, OldWorldIsAbsentButCurrentBattlePacketRetainsOwnSnapshot)
{
	startGame();
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_MASTERIES;
	old.iser.version = ESerializationVersion::NEW_HORIZONS_MASTERIES;
	old.oser & *gameState();
	auto restored = std::make_shared<CGameState>();
	restored->preInit(LIBRARY);
	restored->loadFromMemory(gameState()->saveToMemory());
	ASSERT_TRUE(restored->getCreatureCategory(pixie())) << "Prime current world before old load";
	old.iser.cb = restored.get();
	old.iser.loadingGamestate = true;
	old.iser & *restored;
	EXPECT_FALSE(restored->getCreatureCategory(pixie()));
	startBattle();
	BattleStart outgoing;
	outgoing.battleID = BattleID(0);
	outgoing.info = CMemorySerializer::deepCopy(*battle(), restored.get());
	CMemorySerializer wire;
	wire.oser & outgoing;
	wire.iser.cb = restored.get();
	BattleStart incoming;
	wire.iser & incoming;
	ASSERT_NE(incoming.info, nullptr);
	EXPECT_EQ(incoming.info->battleGetCreatureCategory(pixie()), gameState()->getCreatureCategory(pixie()));
	EXPECT_FALSE(restored->getCreatureCategory(pixie()));
}

TEST_F(NewHorizonsCreatureCategoryStateTest, OldBattleAndResavedAbsenceCannotFallBackToActiveWorld)
{
	startGame();
	startBattle();
	ASSERT_TRUE(gameState()->getCreatureCategory(pixie()));
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_MASTERIES;
	old.iser.version = ESerializationVersion::NEW_HORIZONS_MASTERIES;
	old.oser & *battle();
	old.iser.cb = gameState().get();
	BattleInfo restored(gameState().get());
	ASSERT_TRUE(restored.battleGetCreatureCategory(pixie())) << "Constructor captures active world";
	old.iser & restored;
	EXPECT_FALSE(restored.battleGetCreatureCategory(pixie())) << "Old absence clears constructor capture";
	const auto resaved = CMemorySerializer::deepCopy(restored, gameState().get());
	EXPECT_FALSE(resaved->battleGetCreatureCategory(pixie()));
	EXPECT_TRUE(gameState()->getCreatureCategory(pixie()));
}

TEST_F(NewHorizonsCreatureCategoryStateTest, RealAiHypotheticProxyUsesBattleSnapshotWithoutWorldFallback)
{
	startGame();
	startBattle();
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	CategoryEnvironment environment(gameState());
	HypotheticBattle predicted(&environment, callback);
	EXPECT_EQ(predicted.battleGetCreatureCategory(pixie()), battle()->battleGetCreatureCategory(pixie()));
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_MASTERIES;
	old.iser.version = ESerializationVersion::NEW_HORIZONS_MASTERIES;
	old.oser & *battle();
	old.iser.cb = gameState().get();
	BattleInfo absent(gameState().get());
	old.iser & absent;
	auto absentCallback = std::make_shared<CPlayerBattleCallback>(&absent, PlayerColor(0));
	HypotheticBattle absentPrediction(&environment, absentCallback);
	EXPECT_TRUE(gameState()->getCreatureCategory(pixie()));
	EXPECT_FALSE(absentPrediction.battleGetCreatureCategory(pixie()));
}

TEST_F(NewHorizonsCreatureCategoryStateTest, RealBattleAndAiKeepDifferentMappingAndIdentityFromWorld)
{
	startGame();
	startBattle();
	auto alternative = contextFixture();
	alternative["creatures"]["core:pixie"].String() = "champion";
	alternative["sourceRulesetId"].String() = "new-horizons:otherWorldCategories";
	const auto * creature = pixie().toCreature();
	const auto tier = creature->getLevel();
	const auto attack = creature->getBaseAttack();
	const auto upgrades = creature->upgrades;
	const auto army = attackerSideHero->getArmyStrength();
	const auto count = attackerSideHero->getStackCount(SlotID(0));
	const auto capacity = attackerSideHero->getLeadershipCapacity();
	CategoryWorldCallback otherWorld(*gameState(), alternative);
	ASSERT_EQ(otherWorld.getCreatureCategory(pixie())->category, CreatureCategory::CHAMPION);
	CMemorySerializer wire;
	wire.oser & *battle();
	wire.iser.cb = &otherWorld;
	BattleInfo incoming(&otherWorld);
	ASSERT_EQ(incoming.battleGetCreatureCategory(pixie())->category, CreatureCategory::CHAMPION);
	wire.iser & incoming;
	auto callback = std::make_shared<CPlayerBattleCallback>(&incoming, PlayerColor(0));
	CategoryEnvironment environment(gameState(), &otherWorld);
	HypotheticBattle predicted(&environment, callback);
	const auto view = predicted.battleGetCreatureCategory(pixie());
	ASSERT_TRUE(view);
	EXPECT_EQ(view->category, CreatureCategory::CORE);
	EXPECT_EQ(view->sourceRulesetId, "new-horizons:creatureCategories");
	EXPECT_EQ(otherWorld.getCreatureCategory(pixie())->category, CreatureCategory::CHAMPION);
	EXPECT_EQ(otherWorld.getCreatureCategory(pixie())->sourceRulesetId, "new-horizons:otherWorldCategories");
	EXPECT_EQ(creature->getLevel(), tier);
	EXPECT_EQ(creature->getBaseAttack(), attack);
	EXPECT_EQ(creature->upgrades, upgrades);
	EXPECT_EQ(attackerSideHero->getArmyStrength(), army);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), count);
	const auto afterCapacity = attackerSideHero->getLeadershipCapacity();
	ASSERT_EQ(afterCapacity.has_value(), capacity.has_value());
	if(capacity && afterCapacity)
	{
		EXPECT_EQ(afterCapacity->capacity, capacity->capacity);
		EXPECT_EQ(afterCapacity->used, capacity->used);
		EXPECT_EQ(afterCapacity->movementPercent, capacity->movementPercent);
	}
}
