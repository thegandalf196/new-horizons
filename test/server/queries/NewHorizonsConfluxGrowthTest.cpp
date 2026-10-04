/*
 * NewHorizonsConfluxGrowthTest.cpp, part of VCMI engine
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
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/entities/building/CBuilding.h"
#include "../../../lib/entities/faction/CTown.h"
#include "../../../lib/filesystem/ResourcePath.h"
#include "../../../lib/constants/Enumerations.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"

#ifdef ENABLE_NULLKILLER2_AI
#include "../../../AI/Nullkiller2/Analyzers/ArmyManager.h"
#include "../../../AI/Nullkiller2/Analyzers/BuildAnalyzer.h"
#endif

#include <array>
#include <stdexcept>
#include <vector>

namespace
{
FactionID faction(const char * id)
{
	return FactionID(FactionID::decode(id));
}

CreatureID creature(const char * id)
{
	return CreatureID(CreatureID::decode(id));
}

class NewHorizonsConfluxGrowthTest : public TinyMapGameTest
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
		loaded->overrideGameSetting(EGameSettings::CREATURES_NEW_HORIZONS_CATEGORIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCreatureCategories")));

		JsonNode buildingLimit;
		buildingLimit.Integer() = 32;
		loaded->overrideGameSetting(EGameSettings::TOWNS_BUILDINGS_PER_TURN_CAP, buildingLimit);
		loaded->overrideGameSetting(EGameSettings::CREATURES_ALLOW_RANDOM_SPECIAL_WEEKS, JsonNode(false));

		// TinyH3MBuilder emits the standard Fort + DEFAULT starting set for towns.
		// Remove those authored defaults before CGameState initializes town stocks;
		// all buildings under test are then built through CGameHandler below.
		for(const auto & object : loaded->objects)
		{
			auto * town = dynamic_cast<CGTownInstance *>(object.get());
			if(town && town->getFactionID() == faction("core:conflux"))
			{
				town->removeBuilding(BuildingID::DEFAULT);
				town->removeBuilding(BuildingID::FORT);
			}
		}
	}

	void startConfluxMap()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NewHorizonsConfluxGrowth")
			.playerActive(PlayerColor(0))
			.town({18, 18, 0}, faction("core:conflux"), PlayerColor(0));
		startWithMap(std::move(builder));
	}

	static void grantBuildingResources(NewHorizonsConfluxGrowthTest & test)
	{
		for(const auto resource : {EGameResID::WOOD, EGameResID::ORE, EGameResID::MERCURY,
			EGameResID::SULFUR, EGameResID::CRYSTAL, EGameResID::GEMS, EGameResID::GOLD})
			test.grantResources(PlayerColor(0), GameResID(resource), 100000);
	}
};
}

TEST_F(NewHorizonsConfluxGrowthTest, GardenKeepsPixieAndSpriteStocksIndependentAndGrowthSurvivesSave)
{
	const auto pixie = creature("core:pixie");
	const auto sprite = creature("core:sprite");
	const auto firebird = creature("core:firebird");
	startConfluxMap();
	auto * town = expectAt<CGTownInstance>({18, 18, 0});
	ASSERT_NE(town, nullptr);
	ASSERT_EQ(town->getTown()->creatures.size(), 8u);
	ASSERT_EQ(town->creatures.size(), 8u);
	ASSERT_EQ(town->getTown()->creatures.at(0).size(), 1u);
	ASSERT_EQ(town->getTown()->creatures.at(7).size(), 1u);
	EXPECT_EQ(town->getTown()->creatures.at(0).front(), pixie);
	EXPECT_EQ(town->getTown()->creatures.at(7).front(), sprite);
	EXPECT_EQ(town->getTown()->creatures.at(6).front(), firebird);
	EXPECT_EQ(BuildingID::getLevelIndexFromDwelling(BuildingID::DWELL_LVL_8), 7);
	EXPECT_EQ(BuildingID::getLevelIndexFromDwelling(BuildingID::DWELL_LVL_7), 6);
	ASSERT_NE(pixie.toCreature(), nullptr);
	EXPECT_FALSE(vstd::contains(pixie.toCreature()->upgrades, sprite));
	EXPECT_EQ(gameState()->getCreatureBaseGrowth(pixie), 14);
	EXPECT_EQ(gameState()->getCreatureBaseGrowth(sprite), 10);

	const auto pixieDwelling = BuildingID::getDwellingFromLevel(0, 0);
	const auto spriteDwelling = BuildingID::getDwellingFromLevel(7, 0);
	ASSERT_TRUE(town->getTown()->buildings.contains(pixieDwelling));
	ASSERT_TRUE(town->getTown()->buildings.contains(spriteDwelling));
	ASSERT_TRUE(town->getTown()->buildings.contains(BuildingID::HORDE_1));

	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler gameHandler(server, gameState());
	gameHandler.randomizer->setSeed(211);
	grantBuildingResources(*this);

	// Begin on day one before constructing any dwelling, so each new stock starts
	// at one week of base growth and the following real week boundary adds exactly
	// the ordinary row growth.
	gameHandler.onNewTurn();
	ASSERT_EQ(gameState()->day, 1);

	EXPECT_FALSE(town->hasBuilt(BuildingID::FORT));
	EXPECT_FALSE(town->hasBuilt(pixieDwelling));
	EXPECT_FALSE(town->hasBuilt(spriteDwelling));
	EXPECT_EQ(gameState()->canBuildStructure(town, spriteDwelling), EBuildingState::PREREQUIRES);
	EXPECT_TRUE(town->creatures.at(0).second.empty());
	EXPECT_TRUE(town->creatures.at(7).second.empty());

	// Build the real prerequisite chain through the authoritative handler. This
	// exercises row8's construction path instead of directly editing town stock.
	const std::array<BuildingID, 7> prerequisiteAndDwellings = {
		BuildingID::FORT,
		BuildingID::MAGES_GUILD_1,
		pixieDwelling,
		BuildingID::DWELL_LVL_2,
		BuildingID::DWELL_LVL_3,
		spriteDwelling,
		BuildingID::HORDE_1
	};
	for(const auto building : prerequisiteAndDwellings)
	{
		if(town->hasBuilt(building))
			continue;
		ASSERT_TRUE(gameHandler.buildStructure(town->id, building)) << "failed building " << building;
		EXPECT_TRUE(town->hasBuilt(building));
	}

	ASSERT_EQ(town->creatures.at(0).second, (std::vector<CreatureID>{pixie}));
	ASSERT_EQ(town->creatures.at(7).second, (std::vector<CreatureID>{sprite}));
	ASSERT_EQ(town->creatures.at(0).first, 14u);
	ASSERT_EQ(town->creatures.at(7).first, 10u);
	EXPECT_EQ(town->creatureGrowth(0), 18);
	EXPECT_EQ(town->creatureGrowth(7), 13);
	const int unrelatedRowGrowth = town->creatureGrowth(2);

#ifdef ENABLE_NULLKILLER2_AI
	// BuildingInfo must use the town's explicit Sprite row, not infer a row from
	// Sprite's creature level (which can alias Pixie's row).
	auto armyManager = std::make_unique<NK2AI::ArmyManager>(nullptr, nullptr);
	const auto spriteInfo = NK2AI::BuildingInfo(town->getTown()->buildings.at(spriteDwelling).get(),
		sprite.toCreature(), sprite, town, armyManager);
	const auto pixieInfo = NK2AI::BuildingInfo(town->getTown()->buildings.at(pixieDwelling).get(),
		pixie.toCreature(), pixie, town, armyManager);
	EXPECT_TRUE(spriteInfo.isBuilt);
	EXPECT_EQ(spriteInfo.creatureGrowth, town->creatureGrowth(7));
	EXPECT_EQ(spriteInfo.creatureGrowth, 13);
	EXPECT_EQ(pixieInfo.creatureGrowth, town->creatureGrowth(0));
	EXPECT_EQ(pixieInfo.creatureGrowth, 18);
#endif

	for(int day = 1; day < 8; ++day)
		gameHandler.onNewTurn();
	ASSERT_EQ(gameState()->day, 8);
	EXPECT_EQ(town->creatures.at(0).first, 32u);
	EXPECT_EQ(town->creatures.at(7).first, 23u);
	EXPECT_EQ(town->creatureGrowth(2), unrelatedRowGrowth);

	const auto townId = town->id;
	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredTown = restored.getTown(townId);
	ASSERT_NE(restoredTown, nullptr);
	EXPECT_EQ(restoredTown->getTown()->creatures.size(), 8u);
	ASSERT_EQ(restoredTown->creatures.size(), 8u);
	EXPECT_EQ(restoredTown->creatures.at(0).second, (std::vector<CreatureID>{pixie}));
	EXPECT_EQ(restoredTown->creatures.at(7).second, (std::vector<CreatureID>{sprite}));
	EXPECT_EQ(restoredTown->creatures.at(0).first, 32u);
	EXPECT_EQ(restoredTown->creatures.at(7).first, 23u);
	EXPECT_EQ(restored.getCreatureBaseGrowth(pixie), 14);
	EXPECT_EQ(restored.getCreatureBaseGrowth(sprite), 10);
	EXPECT_EQ(restoredTown->creatureGrowth(0), 18);
	EXPECT_EQ(restoredTown->creatureGrowth(7), 13);

	const auto pixieStockBeforeRecruitment = town->creatures.at(0).first;
	const auto spriteStockBeforeRecruitment = town->creatures.at(7).first;
	ASSERT_TRUE(gameHandler.recruitCreatures(town->id, town->id, pixie, 1, 0, PlayerColor(0)));
	EXPECT_EQ(town->creatures.at(0).first, pixieStockBeforeRecruitment - 1);
	EXPECT_EQ(town->creatures.at(7).first, spriteStockBeforeRecruitment);
	auto pixieSlot = town->getSlotFor(pixie);
	ASSERT_TRUE(pixieSlot.validSlot());
	EXPECT_EQ(town->getStackCount(pixieSlot), 1);

	ASSERT_TRUE(gameHandler.recruitCreatures(town->id, town->id, sprite, 1, 7, PlayerColor(0)));
	EXPECT_EQ(town->creatures.at(0).first, pixieStockBeforeRecruitment - 1);
	EXPECT_EQ(town->creatures.at(7).first, spriteStockBeforeRecruitment - 1);
	auto spriteSlot = town->getSlotFor(sprite);
	ASSERT_TRUE(spriteSlot.validSlot());
	EXPECT_EQ(town->getStackCount(spriteSlot), 1);

	// This intentionally malformed current-format payload has the seven-row
	// stock shape used by pre-Sprite Conflux saves. Loading against the current
	// eight-row static catalogue must fail instead of reinterpreting its stocks.
	town->creatures.resize(7);
	const auto oldRows = gameState()->saveToMemory();
	CGameState rejected;
	rejected.preInit(LIBRARY);
	EXPECT_THROW(rejected.loadFromMemory(oldRows), std::runtime_error);
}

TEST_F(NewHorizonsConfluxGrowthTest, NewConfluxTownStartsWithEightIndependentStockRows)
{
	const auto pixie = creature("core:pixie");
	const auto sprite = creature("core:sprite");
	const auto firebird = creature("core:firebird");
	startConfluxMap();
	const auto * town = expectAt<CGTownInstance>({18, 18, 0});
	ASSERT_NE(town, nullptr);
	EXPECT_EQ(town->getTown()->creatures.size(), 8u);
	EXPECT_EQ(town->creatures.size(), 8u);
	EXPECT_EQ(town->getTown()->creatures.at(0).front(), pixie);
	EXPECT_EQ(town->getTown()->creatures.at(7).front(), sprite);
	EXPECT_EQ(town->getTown()->creatures.at(6).front(), firebird);
	EXPECT_EQ(town->creatures.at(0).second.size(), 0u);
	EXPECT_EQ(town->creatures.at(7).second.size(), 0u);
}

TEST_F(NewHorizonsConfluxGrowthTest, VaultOfAshesAddsFireLineGrowthAtTheWeeklyBoundary)
{
	const auto fireElemental = creature("core:fireElemental");
	const auto energyElemental = creature("core:energyElemental");
	const auto waterElemental = creature("core:waterElemental");
	startConfluxMap();
	auto * town = expectAt<CGTownInstance>({18, 18, 0});
	ASSERT_NE(town, nullptr);
	ASSERT_EQ(town->getTown()->creatures.size(), 8u);
	ASSERT_EQ(town->creatures.size(), 8u);
	ASSERT_EQ(town->getTown()->hordeLvl.at(1), 3);
	ASSERT_TRUE(town->getTown()->buildings.contains(BuildingID::HORDE_2));
	ASSERT_NE(fireElemental.toCreature(), nullptr);
	ASSERT_NE(energyElemental.toCreature(), nullptr);
	ASSERT_NE(waterElemental.toCreature(), nullptr);
	EXPECT_EQ(town->getTown()->creatures.at(3).front(), fireElemental);
	EXPECT_EQ(town->creatureBaseGrowth(fireElemental), 4);
	EXPECT_EQ(town->creatureHordeGrowth(fireElemental), 2);
	EXPECT_EQ(town->creatureHordeGrowth(energyElemental), 2)
		<< "The Fire/Energy upgrade pair shares its saved growth-line horde override";

	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler gameHandler(server, gameState());
	gameHandler.randomizer->setSeed(227);
	grantBuildingResources(*this);
	gameHandler.onNewTurn();
	ASSERT_EQ(gameState()->day, 1);

	const auto fireDwelling = BuildingID::getDwellingFromLevel(3, 0);
	const auto waterDwelling = BuildingID::getDwellingFromLevel(2, 0);
	ASSERT_TRUE(town->getTown()->buildings.contains(fireDwelling));
	ASSERT_TRUE(town->getTown()->buildings.contains(waterDwelling));
	ASSERT_FALSE(town->hasBuilt(BuildingID::HORDE_2));

	const std::array<BuildingID, 6> prerequisiteBuildings = {
		BuildingID::FORT,
		BuildingID::MAGES_GUILD_1,
		BuildingID::DWELL_LVL_1,
		BuildingID::DWELL_LVL_2,
		waterDwelling,
		fireDwelling
	};
	for(size_t index = 0; index + 1 < prerequisiteBuildings.size(); ++index)
	{
		const auto building = prerequisiteBuildings[index];
		ASSERT_TRUE(gameHandler.buildStructure(town->id, building)) << "failed building " << building;
		EXPECT_TRUE(town->hasBuilt(building));
	}
	EXPECT_EQ(gameState()->canBuildStructure(town, BuildingID::HORDE_2), EBuildingState::PREREQUIRES)
		<< "Vault of Ashes requires the Fire Elemental dwelling";
	ASSERT_TRUE(gameHandler.buildStructure(town->id, fireDwelling));
	EXPECT_TRUE(town->hasBuilt(fireDwelling));
	EXPECT_EQ(gameState()->canBuildStructure(town, BuildingID::HORDE_2), EBuildingState::ALLOWED);

	constexpr int fireRow = 3;
	constexpr int waterRow = 2;
	ASSERT_EQ(town->creatures.at(fireRow).second, (std::vector<CreatureID>{fireElemental}));
	ASSERT_EQ(town->creatures.at(fireRow).first, 4u);
	EXPECT_EQ(town->creatureGrowth(fireRow), 4) << "An unbuilt Vault does not activate its +2 growth";
	const auto fireStockBeforeVault = town->creatures.at(fireRow).first;
	const auto waterStockBeforeVault = town->creatures.at(waterRow).first;
	const int waterGrowthBeforeVault = town->creatureGrowth(waterRow);

#ifdef ENABLE_NULLKILLER2_AI
	// An unbuilt Horde 2 is already mapped to the Fire Elemental row, so the
	// shared AI building estimate sees its prospective +2 without building it.
	auto armyManager = std::make_unique<NK2AI::ArmyManager>(nullptr, nullptr);
	const auto prospectiveVaultInfo = NK2AI::BuildingInfo(
		town->getTown()->buildings.at(BuildingID::HORDE_2).get(), fireElemental.toCreature(),
		fireElemental, town, armyManager);
	EXPECT_FALSE(prospectiveVaultInfo.isBuilt);
	EXPECT_EQ(prospectiveVaultInfo.creatureGrowth, 2);
#endif

	ASSERT_TRUE(gameHandler.buildStructure(town->id, BuildingID::HORDE_2));
	EXPECT_TRUE(town->hasBuilt(BuildingID::HORDE_2));
	EXPECT_EQ(town->creatures.at(fireRow).first, fireStockBeforeVault)
		<< "Construction changes next week's growth, not the stock already generated on day one";
	EXPECT_EQ(town->creatures.at(waterRow).first, waterStockBeforeVault);
	EXPECT_EQ(town->creatureGrowth(fireRow), 6);
	EXPECT_EQ(town->creatureGrowth(waterRow), waterGrowthBeforeVault);

	const auto energyDwelling = BuildingID::getDwellingFromLevel(3, 1);
	ASSERT_TRUE(town->getTown()->buildings.contains(energyDwelling));
	ASSERT_TRUE(gameHandler.buildStructure(town->id, energyDwelling));
	ASSERT_EQ(town->creatures.at(fireRow).second,
		(std::vector<CreatureID>{fireElemental, energyElemental}));
	EXPECT_EQ(town->creatureGrowth(fireRow), 6)
		<< "Upgrading the shared Fire/Energy stock row does not stack the horde addition";
	EXPECT_EQ(town->creatureGrowth(waterRow), waterGrowthBeforeVault)
		<< "Vault growth remains confined to the Fire Elemental line";

#ifdef ENABLE_NULLKILLER2_AI
	const auto builtVaultInfo = NK2AI::BuildingInfo(
		town->getTown()->buildings.at(BuildingID::HORDE_2).get(), fireElemental.toCreature(),
		fireElemental, town, armyManager);
	EXPECT_TRUE(builtVaultInfo.isBuilt);
	EXPECT_EQ(builtVaultInfo.creatureGrowth, 6)
		<< "The built-row AI forecast uses the same authoritative growth query";
#endif

	for(int day = 1; day < 8; ++day)
		gameHandler.onNewTurn();
	ASSERT_EQ(gameState()->day, 8);
	EXPECT_EQ(town->creatures.at(fireRow).first, 10u);
	EXPECT_EQ(town->creatures.at(waterRow).first, waterStockBeforeVault + waterGrowthBeforeVault);

	const auto townId = town->id;
	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredTown = restored.getTown(townId);
	ASSERT_NE(restoredTown, nullptr);
	ASSERT_EQ(restoredTown->getTown()->hordeLvl.at(1), 3);
	ASSERT_EQ(restoredTown->creatures.size(), 8u);
	EXPECT_TRUE(restoredTown->hasBuilt(BuildingID::HORDE_2));
	EXPECT_EQ(restoredTown->creatureBaseGrowth(fireElemental), 4);
	EXPECT_EQ(restoredTown->creatureHordeGrowth(fireElemental), 2);
	EXPECT_EQ(restoredTown->creatureHordeGrowth(energyElemental), 2);
	EXPECT_EQ(restoredTown->creatures.at(fireRow).second,
		(std::vector<CreatureID>{fireElemental, energyElemental}));
	EXPECT_EQ(restoredTown->creatures.at(fireRow).first, 10u);
	EXPECT_EQ(restoredTown->creatureGrowth(fireRow), 6);
	EXPECT_EQ(restoredTown->creatures.at(waterRow).first,
		waterStockBeforeVault + waterGrowthBeforeVault);
	EXPECT_EQ(restoredTown->creatureGrowth(waterRow), waterGrowthBeforeVault);
}
