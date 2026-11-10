/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../mock/GameHandlerTestServer.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGResource.h"
#include "../../lib/mapObjects/CGDwelling.h"
#include "../../lib/mapObjects/CRewardableObject.h"
#include "../../lib/mapObjects/NewHorizonsNeutralArmies.h"
#include "../../lib/mapObjects/NewHorizonsNeutralCalibration.h"
#include "../../lib/mapObjects/NewHorizonsRecruitmentGrowth.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include "../../lib/mapObjectConstructors/CRewardableConstructor.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../server/CGameHandler.h"
#include "../../server/processors/NewTurnProcessor.h"
#include <tuple>
#include <vstd/RNG.h>

namespace
{
class CommandNeutralWorldTest : public TinyMapGameTest
{
protected:
	int percent = 50;
	Services * gameServices() override { return LIBRARY; }
	void configureWorldInitialization(StartInfo &, GameRandomizer & rng) const override { rng.setSeed(1337); }
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		loaded->overrideGameSetting(EGameSettings::CREATURES_NEUTRAL_ARMY_SIZE_PERCENT, JsonNode(percent));
		loaded->overrideGameSetting(EGameSettings::CREATURES_RECRUITMENT_GROWTH_PERCENT, JsonNode(37));
		loaded->overrideGameSetting(EGameSettings::CREATURES_WEEKLY_GROWTH_PERCENT, JsonNode(13));
		const std::string generation = R"({"version":1,"enabled":true,
			"stages":[{"heroLevel":1,"weeks":1,"unlockedLines":3,"troopGold":4000}],
			"unlockWeeks":[1,1,1,2,2,3,4]})";
		loaded->overrideGameSetting(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION,
			JsonNode(generation.data(), generation.size(), "fresh Command calibration override"));
		for(auto * pile : loaded->getObjects<CGResource>())
			for(int slot = 0; slot < 7; ++slot)
				pile->putStack(SlotID(slot), std::make_unique<CStackInstance>(gameState().get(), champion(), 101));
	}
	static CreatureID champion() { return CreatureID(CreatureID::decode("core:angel")); }
	void prepare()
	{
		const CreatureID pikeman(CreatureID::decode("core:pikeman"));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0)).heroGarrison({{pikeman, 7}})
			.hero({8, 8, 0}, HeroTypeID(1), PlayerColor(1)).heroGarrison({{pikeman, 7}})
			.monster({12, 6, 0}, champion(), 101)
			.resource({16, 10, 0}, GameResID(EGameResID::WOOD), 1);
		startWithMap(std::move(builder));
	}
};
}

TEST_F(CommandNeutralWorldTest, FreshCaptureDisablesOnlyRetiredCalibrationAndKeepsSevenChampionGuardSlots)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto & settings = map()->getSettings();
	EXPECT_EQ(settings.getValue(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION),
		newHorizonsNeutralArmies::calibration::disabledSnapshot());
	EXPECT_EQ(newHorizonsNeutralArmies::percentage(settings), 50);
	EXPECT_EQ(newHorizonsRecruitmentGrowth::percentage(settings), 37);
	EXPECT_EQ(settings.getInteger(EGameSettings::CREATURES_WEEKLY_GROWTH_PERCENT), 13);
	const auto * monster = findFirst<CGCreature>();
	ASSERT_NE(monster, nullptr);
	EXPECT_EQ(monster->getStackCount(SlotID(0)), 51);
	EXPECT_EQ(monster->temppower, 51000);
	const auto * pile = findFirst<CGResource>();
	ASSERT_NE(pile, nullptr);
	EXPECT_EQ(pile->stacksCount(), 7);
	for(int slot = 0; slot < 7; ++slot)
	{
		EXPECT_EQ(pile->getStackCount(SlotID(slot)), 51);
		EXPECT_EQ(pile->getStackPtr(SlotID(slot))->getId(), champion());
	}
	EXPECT_EQ(newHorizonsNeutralArmies::generatedCounts(*pile, {{champion(), 101}, {champion(), 3}}),
		(std::vector<TQuantity>{51, 2}));
	for(const auto * hero : map()->getObjects<CGHeroInstance>())
	{
		ASSERT_TRUE(hero->getArmyCommand());
		EXPECT_FALSE(hero->getLeadershipCapacity());
		EXPECT_EQ(hero->getStackCount(SlotID(0)), 7);
	}
}

TEST_F(CommandNeutralWorldTest, IdentityPercentPreservesLargeChampionCountsWithoutAHeroLevelOneLimit)
{
	percent = 100;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(findFirst<CGCreature>()->getStackCount(SlotID(0)), 101);
	const auto * pile = findFirst<CGResource>();
	ASSERT_NE(pile, nullptr);
	for(int slot = 0; slot < 7; ++slot)
		EXPECT_EQ(pile->getStackCount(SlotID(slot)), 101);
}

namespace
{
template<typename Value>
auto neutralBytes(const Value & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}

std::shared_ptr<CGameState> loadNeutralWorld(std::vector<std::byte> saved)
{
	auto result = std::make_shared<CGameState>();
	result->preInit(LIBRARY);
	result->loadFromMemory(std::move(saved));
	return result;
}

auto neutralFormation(const CMap & map)
{
	std::vector<std::tuple<ObjectInstanceID, int3, SlotID, CreatureID, TQuantity>> result;
	for(const auto * object : map.getObjects())
		if(const auto * army = dynamic_cast<const CArmedInstance *>(object); army && newHorizonsNeutralArmies::eligible(*army))
			for(const auto & [slot, stack] : army->Slots())
				result.emplace_back(army->id, army->anchorPos(), slot, stack->getId(), stack->getCount());
	return result;
}

class StockNeutralWorldTest : public TinyMapGameTest
{
protected:
	bool explicitStock = false;
	std::optional<int32_t> mapPercent;
	std::optional<int32_t> lobbyPercent;
	mutable GameRandomizer * initializingRandomizer = nullptr;
	std::vector<std::byte> initializedRandomState;
	Services * gameServices() override { return LIBRARY; }
	void configureWorldInitialization(StartInfo & info, GameRandomizer & rng) const override
	{
		info.extraOptionsInfo.neutralArmySizePercent = lobbyPercent;
		rng.setSeed(1337);
		initializingRandomizer = &rng;
	}
	void onMapStarted() override
	{
		initializedRandomState = neutralBytes(*initializingRandomizer);
		initializingRandomizer = nullptr;
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		if(explicitStock)
		{
			loaded->overrideGameSetting(EGameSettings::CREATURES_NEUTRAL_ARMY_SIZE_PERCENT, JsonNode(100));
			loaded->overrideGameSetting(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION,
				newHorizonsNeutralArmies::calibration::disabledSnapshot());
			loaded->overrideGameSetting(EGameSettings::CREATURES_WEEKLY_GROWTH_PERCENT, JsonNode(10));
		}
		if(mapPercent)
			loaded->overrideGameSetting(EGameSettings::CREATURES_NEUTRAL_ARMY_SIZE_PERCENT, JsonNode(*mapPercent));
		for(auto * pile : loaded->getObjects<CGResource>())
			for(int slot = 0; slot < 7; ++slot)
				pile->putStack(SlotID(slot), std::make_unique<CStackInstance>(gameState().get(), champion(), 101));
	}
	static CreatureID champion() { return CreatureID(CreatureID::decode("core:angel")); }
	CGDwelling * externalDwelling() const
	{
		for(auto * dwelling : map()->getObjects<CGDwelling>())
			if(dwelling->ID == Obj::CREATURE_GENERATOR1)
				return dwelling;
		return nullptr;
	}
	void prepare()
	{
		const CreatureID pikeman(CreatureID::decode("core:pikeman"));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0)).heroGarrison({{pikeman, 7}})
			.hero({8, 8, 0}, HeroTypeID(1), PlayerColor(1)).heroGarrison({{pikeman, 7}})
			.monster({12, 6, 0}, champion(), 101)
			.monster({12, 9, 0}, champion(), 0)
			.monster({12, 12, 0}, champion(), 3999)
			.monster({12, 15, 0}, champion(), 4001)
			.resource({16, 10, 0}, GameResID(EGameResID::WOOD), 1)
			.town({25, 25, 0}, FactionID::CASTLE, PlayerColor::NEUTRAL).townGarrison({{champion(), 101}})
			.dwelling({8, 17, 0}, MapObjectSubID(56), PlayerColor::NEUTRAL);
		startWithMap(std::move(builder));
	}
};
}

TEST_F(StockNeutralWorldTest, FreshDefaultsMatchExplicitStockGenerationWithTheSameSeed)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto & settings = map()->getSettings();
	EXPECT_EQ(newHorizonsNeutralArmies::percentage(settings), 100);
	EXPECT_EQ(settings.getInteger(EGameSettings::CREATURES_WEEKLY_GROWTH_PERCENT), 10);
	EXPECT_EQ(settings.getInteger(EGameSettings::CREATURES_WEEKLY_GROWTH_CAP), 4000);
	EXPECT_EQ(newHorizonsRecruitmentGrowth::percentage(settings), 50);
	EXPECT_EQ(settings.getValue(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION),
		newHorizonsNeutralArmies::calibration::disabledSnapshot());
	const auto & capabilities = settings.getValue(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES);
	EXPECT_TRUE(capabilities.Struct().contains("armyCommand"));
	EXPECT_FALSE(capabilities.Struct().contains("leadership"));
	EXPECT_FALSE(capabilities.Struct().contains("classProfiles"));
	EXPECT_FALSE(gameState()->getStartInfo()->extraOptionsInfo.neutralArmySizePercent);
	const auto monsters = map()->getObjects<CGCreature>();
	ASSERT_EQ(monsters.size(), 4);
	EXPECT_EQ(monsters[0]->getStackCount(SlotID(0)), 101);
	EXPECT_GE(monsters[1]->getStackCount(SlotID(0)), champion().toCreature()->getAdvMapAmountMin());
	EXPECT_LE(monsters[1]->getStackCount(SlotID(0)), champion().toCreature()->getAdvMapAmountMax());
	EXPECT_EQ(monsters[2]->getStackCount(SlotID(0)), 3999);
	EXPECT_EQ(monsters[3]->getStackCount(SlotID(0)), 4001); // Growth cap is not an initial-army clamp.
	EXPECT_EQ(findFirst<CGTownInstance>()->getStackCount(SlotID(0)), 101);
	const auto * guards = findFirst<CGResource>();
	ASSERT_NE(guards, nullptr);
	ASSERT_EQ(guards->stacksCount(), 7);
	for(const auto & [slot, stack] : guards->Slots())
	{
		EXPECT_EQ(stack->getId(), champion());
		EXPECT_EQ(stack->getCount(), 101);
	}
	const auto formation = neutralFormation(*map());
	const auto rng = initializedRandomState;
	ASSERT_NE(externalDwelling(), nullptr);
	const auto dwellingStock = externalDwelling()->creatures;
	const auto townStock = findFirst<CGTownInstance>()->creatures;
	// Rebuild the same H3M through actual initialization, explicitly selecting the
	// upstream count/growth identity. This catches a second default multiplier.
	TinyMapGameTest::TearDown();
	TinyMapGameTest::SetUp();
	explicitStock = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(neutralFormation(*map()), formation);
	EXPECT_EQ(initializedRandomState, rng);
	ASSERT_NE(externalDwelling(), nullptr);
	EXPECT_EQ(externalDwelling()->creatures, dwellingStock);
	EXPECT_EQ(findFirst<CGTownInstance>()->creatures, townStock);
	EXPECT_EQ(newHorizonsRecruitmentGrowth::percentage(map()->getSettings()), 50);
}

TEST_F(StockNeutralWorldTest, AuthoredRelativePercentageSurvivesAnUnsetScenarioSlider)
{
	mapPercent = 75;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(gameState()->getStartInfo()->extraOptionsInfo.neutralArmySizePercent);
	EXPECT_EQ(newHorizonsNeutralArmies::percentage(map()->getSettings()), 75);
	EXPECT_EQ(findFirst<CGCreature>()->getStackCount(SlotID(0)), 76);
	EXPECT_EQ(findFirst<CGTownInstance>()->getStackCount(SlotID(0)), 76);
}

TEST_F(StockNeutralWorldTest, ExplicitScenarioIdentityOverridesOnlyTheAuthoredRelativePercentage)
{
	mapPercent = 75;
	lobbyPercent = 100;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(newHorizonsNeutralArmies::percentage(map()->getSettings()), 100);
	EXPECT_EQ(findFirst<CGCreature>()->getStackCount(SlotID(0)), 101);
	EXPECT_EQ(newHorizonsRecruitmentGrowth::percentage(map()->getSettings()), 50);
}

TEST_F(StockNeutralWorldTest, StockWeeklyGrowthKeepsFractionalPowerNoGrowthFlagsAndTheOriginalCap)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	GameHandlerTestServer server(gameState());
	CGameHandler handler(server, gameState());
	const auto monsters = map()->getObjects<CGCreature>();
	monsters[1]->notGrowingTeam = true;
	const auto frozenCount = monsters[1]->getStackCount(SlotID(0));
	const auto frozenPower = monsters[1]->temppower;
	const auto rng = neutralBytes(*handler.randomizer);
	gameState()->day = 8;
	for(const auto * monster : monsters)
		monster->newTurn(handler, *handler.randomizer);
	EXPECT_EQ(monsters[0]->getStackCount(SlotID(0)), 111);
	EXPECT_EQ(monsters[0]->temppower, 111100);
	EXPECT_EQ(monsters[1]->getStackCount(SlotID(0)), frozenCount);
	EXPECT_EQ(monsters[1]->temppower, frozenPower);
	EXPECT_EQ(monsters[2]->getStackCount(SlotID(0)), 4000);
	EXPECT_EQ(monsters[2]->temppower, 4398900);
	EXPECT_EQ(monsters[3]->getStackCount(SlotID(0)), 4001);
	gameState()->day = 15;
	monsters[0]->newTurn(handler, *handler.randomizer);
	EXPECT_EQ(monsters[0]->getStackCount(SlotID(0)), 122);
	EXPECT_EQ(monsters[0]->temppower, 122210);
	EXPECT_EQ(neutralBytes(*handler.randomizer), rng);
}

TEST_F(StockNeutralWorldTest, DwellingGuardRenewalUsesFullBaseGrowthWhileRecruitmentStaysAtFiftyPercent)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * dwelling = externalDwelling();
	ASSERT_NE(dwelling, nullptr);
	dwelling->creatures = {{17, {champion()}}};
	dwelling->clearSlots();
	GameHandlerTestServer server(gameState());
	CGameHandler handler(server, gameState());
	for(const int day : {8, 15})
	{
		gameState()->day = day;
		static_cast<const IObjectInterface *>(dwelling)->newTurn(handler, *handler.randomizer);
		EXPECT_EQ(dwelling->getStackCount(SlotID(0)), gameState()->getCreatureBaseGrowth(champion()) * 3);
		EXPECT_EQ(dwelling->creatures.front().first,
			newHorizonsRecruitmentGrowth::scale(gameState()->getCreatureBaseGrowth(champion()), 50));
	}
}

TEST_F(StockNeutralWorldTest, LoadResaveAndDifferentLobbyChoiceNeverRescaleExistingCountsOrPower)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * monster = findFirst<CGCreature>();
	monster->getStackPtr(SlotID(0))->setCount(137);
	monster->temppower = 137519;
	const auto formation = neutralFormation(*map());
	auto restored = loadNeutralWorld(gameState()->saveToMemory());
	StartInfo incoming = *restored->getStartInfo();
	incoming.mode = EStartMode::LOAD_GAME;
	incoming.extraOptionsInfo.neutralArmySizePercent = 1;
	restored->updateOnLoad(incoming);
	EXPECT_FALSE(restored->getStartInfo()->extraOptionsInfo.neutralArmySizePercent);
	EXPECT_EQ(neutralFormation(restored->getMap()), formation);
	EXPECT_EQ(restored->getMap().getObjects<CGCreature>().front()->temppower, 137519);
	auto reread = loadNeutralWorld(restored->saveToMemory());
	EXPECT_EQ(neutralFormation(reread->getMap()), formation);
	EXPECT_EQ(reread->getMap().getObjects<CGCreature>().front()->temppower, 137519);
	EXPECT_EQ(newHorizonsRecruitmentGrowth::percentage(reread->getSettings()), 50);
}

TEST_F(StockNeutralWorldTest, MonthlyPopulationMatchesUnscaledProducerCountsTilesAndRandomStream)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto reference = loadNeutralWorld(gameState()->saveToMemory());
	GameHandlerTestServer server(gameState()), referenceServer(reference);
	CGameHandler handler(server, gameState()), stock(referenceServer, reference);
	handler.randomizer->setSeed(2121);
	stock.randomizer->setSeed(2121);
	std::vector<int3> tiles;
	reference->getFreeTiles(tiles, true);
	const auto amount = tiles.size() / 200;
	ASSERT_GT(amount, 0);
	RandomGeneratorUtil::randomShuffle(tiles, stock.getRandomGenerator());
	for(size_t i = 0; i < amount; ++i)
	{
		const auto raw = champion().toCreature()->getRandomAmount(stock.getRandomGenerator());
		stock.createWanderingMonster(tiles[i], champion(), raw);
	}
	handler.spawnWanderingMonsters(champion());
	EXPECT_EQ(neutralFormation(*map()), neutralFormation(reference->getMap()));
	EXPECT_EQ(neutralBytes(*handler.randomizer), neutralBytes(*stock.randomizer));
}

TEST_F(StockNeutralWorldTest, BankRenewalMatchesRawConfigurationAndKeepsAllRewardsAndRandomStreams)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	GameHandlerTestServer server(gameState());
	CGameHandler handler(server, gameState());
	auto object = handler.createNewObject({20, 14, 0}, Obj::DRAGON_UTOPIA, MapObjectSubID(0));
	handler.newObject(object, PlayerColor(0));
	auto * bank = dynamic_cast<CRewardableObject *>(object.get());
	ASSERT_NE(bank, nullptr);
	const auto constructor = std::dynamic_pointer_cast<const CRewardableConstructor>(bank->getObjectHandler());
	ASSERT_NE(constructor, nullptr);
	bank->configuration.resetParameters.days = 1;
	bank->configuration.resetParameters.rewards = true;
	gameState()->day = 2;
	handler.randomizer->setSeed(1337);
	GameRandomizer reference(*gameState());
	CMemorySerializer copy(neutralBytes(*handler.randomizer));
	copy.iser & reference;
	const auto raw = constructor->generateConfiguration(bank->cb, reference, bank->ID, bank->configuration.variables.preset);
	bank->newTurn(handler, *handler.randomizer);
	ASSERT_EQ(bank->configuration.info.size(), raw.info.size());
	int slot = 0;
	for(size_t i = 0; i < raw.info.size(); ++i)
	{
		EXPECT_EQ(neutralBytes(bank->configuration.info[i].reward), neutralBytes(raw.info[i].reward));
		for(const auto & guard : raw.info[i].reward.guards)
			EXPECT_EQ(bank->getStackCount(SlotID(slot++)), guard.getCount());
	}
	EXPECT_GT(slot, 0);
	EXPECT_EQ(neutralBytes(*handler.randomizer), neutralBytes(reference));
}

TEST_F(StockNeutralWorldTest, NeutralTownReinforcementsUseFullBaseGrowthIndependentOfRecruitmentPercentage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	gameState()->day = 7;
	const auto saved = gameState()->saveToMemory();
	int reinforced = 0;
	for(int seed = 1; seed <= 12; ++seed)
	{
		auto world = loadNeutralWorld(saved);
		GameHandlerTestServer server(world);
		CGameHandler handler(server, world);
		handler.randomizer->setSeed(seed);
		NewTurnProcessor(&handler).onNewTurn();
		const auto * town = world->getMap().getObjects<CGTownInstance>().front();
		EXPECT_EQ(town->getStackCount(SlotID(0)), 101); // Week two cannot select tier seven.
		for(const auto & [slot, stack] : town->Slots())
			if(slot != SlotID(0))
			{
				EXPECT_EQ(stack->getCount(), world->getCreatureBaseGrowth(stack->getCreatureID()));
				++reinforced;
			}
		EXPECT_EQ(newHorizonsRecruitmentGrowth::percentage(world->getSettings()), 50);
	}
	EXPECT_GT(reinforced, 0);
}
