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
		if(!useNewHorizonsMagicRules)
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
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

	void startTowerMap(bool useMagicRules = true, bool includeConflux = false)
	{
		useNewHorizonsMagicRules = useMagicRules;

		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NewHorizonsTowerLibraryGrowth")
			.playerActive(PlayerColor(0))
			.town({18, 18, 0}, faction("core:tower"), PlayerColor(0));
		if(includeConflux)
			builder.town({27, 18, 0}, faction("core:conflux"), PlayerColor(0));
		startWithMap(std::move(builder));
	}

	void startConfluxMap()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NewHorizonsConfluxGrowth")
			.playerActive(PlayerColor(0))
			.town({18, 18, 0}, faction("core:conflux"), PlayerColor(0));
		startWithMap(std::move(builder));
	}

	bool useNewHorizonsMagicRules = true;

	static void grantBuildingResources(NewHorizonsConfluxGrowthTest & test)
	{
		for(const auto resource : {EGameResID::WOOD, EGameResID::ORE, EGameResID::MERCURY,
			EGameResID::SULFUR, EGameResID::CRYSTAL, EGameResID::GEMS, EGameResID::GOLD})
			test.grantResources(PlayerColor(0), GameResID(resource), 100000);
	}
};
}

TEST_F(NewHorizonsConfluxGrowthTest, TowerLibraryHelperIsScopedToTowerMageLinesAndSavedRules)
{
	const auto mage = creature("core:mage");
	const auto archMage = creature("core:archMage");
	const auto genie = creature("core:genie");
	startTowerMap(true, true);

	auto * tower = expectAt<CGTownInstance>({18, 18, 0});
	auto * conflux = expectAt<CGTownInstance>({27, 18, 0});
	ASSERT_NE(tower, nullptr);
	ASSERT_NE(conflux, nullptr);

	EXPECT_EQ(tower->creatureBuildingGrowth(BuildingID::SPECIAL_3, mage), 1);
	EXPECT_EQ(tower->creatureBuildingGrowth(BuildingID::SPECIAL_3, archMage), 1);
	EXPECT_EQ(tower->creatureBuildingGrowth(BuildingID::SPECIAL_3, genie), 0);
	EXPECT_EQ(tower->creatureBuildingGrowth(BuildingID::SPECIAL_2, mage), 0);
	EXPECT_EQ(conflux->creatureBuildingGrowth(BuildingID::SPECIAL_3, mage), 0);
}

TEST_F(NewHorizonsConfluxGrowthTest, TowerLibraryHelperIsInactiveWithoutSavedNewHorizonsRules)
{
	startTowerMap(false);
	const auto * tower = expectAt<CGTownInstance>({18, 18, 0});
	ASSERT_NE(tower, nullptr);

	EXPECT_EQ(tower->creatureBuildingGrowth(BuildingID::SPECIAL_3, creature("core:mage")), 0);
	EXPECT_EQ(tower->creatureBuildingGrowth(BuildingID::SPECIAL_3, creature("core:archMage")), 0);
}

#ifdef ENABLE_NULLKILLER2_AI
TEST_F(NewHorizonsConfluxGrowthTest, LibraryCandidateValuesMageRowAndBuiltLibraryTracksArchMage)
{
	const auto mage = creature("core:mage");
	const auto archMage = creature("core:archMage");
	startTowerMap();
	auto * town = expectAt<CGTownInstance>({18, 18, 0});
	ASSERT_NE(town, nullptr);

	const auto & configuredRows = town->getTown()->creatures;
	const auto mageRow = std::find_if(configuredRows.begin(), configuredRows.end(), [mage](const auto & row)
	{
		return std::find(row.begin(), row.end(), mage) != row.end();
	});
	ASSERT_NE(mageRow, configuredRows.end());
	const auto rowIndex = static_cast<int>(std::distance(configuredRows.begin(), mageRow));
	ASSERT_EQ(rowIndex, 4) << "The authored Tower row swap places Mages in dwelling row five";
	ASSERT_EQ(*mageRow, (std::vector<CreatureID>{mage, archMage}));

	const auto mageDwelling = BuildingID::getDwellingFromLevel(rowIndex, 0);
	const auto upgradedMageDwelling = BuildingID::getDwellingFromLevel(rowIndex, 1);
	ASSERT_TRUE(town->getTown()->buildings.contains(mageDwelling));
	ASSERT_TRUE(town->getTown()->buildings.contains(upgradedMageDwelling));

	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler gameHandler(server, gameState());
	gameHandler.randomizer->setSeed(251);
	grantBuildingResources(*this);
	gameHandler.onNewTurn();
	ASSERT_EQ(gameState()->day, 1);

	// Satisfy the Tower row and Library prerequisites through accepted builds.
	// The mage row is the fifth dwelling after the authored Genie/Mage swap.
	const std::array<BuildingID, 10> prerequisites = {
		BuildingID::FORT,
		BuildingID::MAGES_GUILD_1,
		BuildingID::DWELL_LVL_1,
		BuildingID::MAGES_GUILD_2,
		BuildingID::DWELL_LVL_2,
		BuildingID::DWELL_LVL_3,
		BuildingID::MAGES_GUILD_3,
		BuildingID::DWELL_LVL_4,
		BuildingID::MAGES_GUILD_4,
		mageDwelling
	};
	for(const auto building : prerequisites)
	{
		if(town->hasBuilt(building))
			continue;
		ASSERT_TRUE(gameHandler.buildStructure(town->id, building)) << "failed building " << building;
		EXPECT_TRUE(town->hasBuilt(building));
	}
	ASSERT_FALSE(town->hasBuilt(BuildingID::SPECIAL_3));
	ASSERT_EQ(town->creatures.at(static_cast<size_t>(rowIndex)).second, (std::vector<CreatureID>{mage}));

	auto callback = makeCallback(PlayerColor(0));
	auto armyManager = std::make_unique<NK2AI::ArmyManager>(nullptr, nullptr);
	const auto prospectiveLibrary = NK2AI::BuildAnalyzer::getBuildingOrPrerequisite(
		town, BuildingID::SPECIAL_3, armyManager, callback);
	EXPECT_EQ(prospectiveLibrary.id, BuildingID::SPECIAL_3);
	EXPECT_TRUE(prospectiveLibrary.isBuildable);
	EXPECT_FALSE(prospectiveLibrary.isBuilt);
	EXPECT_EQ(prospectiveLibrary.creatureID, mage);
	EXPECT_EQ(prospectiveLibrary.baseCreatureID, mage);
	EXPECT_EQ(prospectiveLibrary.creatureGrowth, 1);
	ASSERT_NE(mage.toCreature(), nullptr);
	EXPECT_EQ(prospectiveLibrary.armyStrength, mage.toCreature()->getAIValue());
	EXPECT_EQ(prospectiveLibrary.armyCost, mage.toCreature()->getFullRecruitCost());

	const int mageGrowthBeforeLibrary = town->creatureGrowth(rowIndex);
	ASSERT_TRUE(gameHandler.buildStructure(town->id, BuildingID::SPECIAL_3));
	EXPECT_EQ(town->creatureGrowth(rowIndex), mageGrowthBeforeLibrary + 1);

	ASSERT_TRUE(gameHandler.buildStructure(town->id, upgradedMageDwelling));
	ASSERT_TRUE(vstd::contains(town->creatures.at(static_cast<size_t>(rowIndex)).second, archMage));
	EXPECT_EQ(town->creatureBuildingGrowth(BuildingID::SPECIAL_3, archMage), 1);
	EXPECT_EQ(town->creatureGrowth(rowIndex), town->creatureBaseGrowth(archMage) + 1);

	const auto builtLibrary = NK2AI::BuildAnalyzer::getBuildingOrPrerequisite(
		town, BuildingID::SPECIAL_3, armyManager, callback);
	EXPECT_TRUE(builtLibrary.isBuilt);
	EXPECT_EQ(builtLibrary.creatureID, archMage);
	EXPECT_EQ(builtLibrary.creatureGrowth, 1)
		<< "A built Library candidate stays a fixed contribution rather than reusing the full row growth";
	ASSERT_NE(archMage.toCreature(), nullptr);
	EXPECT_EQ(builtLibrary.armyStrength, archMage.toCreature()->getAIValue());
	EXPECT_EQ(builtLibrary.armyCost, archMage.toCreature()->getFullRecruitCost());
}
#endif

TEST_F(NewHorizonsConfluxGrowthTest, GardenKeepsPixieAndSpriteInOneStockRowAcrossUpgradeAndSave)
{
	const auto pixie = creature("core:pixie");
	const auto sprite = creature("core:sprite");
	const auto firebird = creature("core:firebird");
	const auto spriteDwelling = BuildingID::getDwellingFromLevel(0, 1);
	startConfluxMap();
	auto * town = expectAt<CGTownInstance>({18, 18, 0});
	ASSERT_NE(town, nullptr);
	ASSERT_EQ(town->getTown()->creatures.size(), 7u);
	ASSERT_EQ(town->creatures.size(), 7u);
	EXPECT_EQ(town->getTown()->creatures.at(0), (std::vector<CreatureID>{pixie, sprite}));
	EXPECT_EQ(town->getTown()->creatures.at(6).front(), firebird);
	ASSERT_NE(pixie.toCreature(), nullptr);
	EXPECT_TRUE(vstd::contains(pixie.toCreature()->upgrades, sprite));
	EXPECT_EQ(gameState()->getCreatureBaseGrowth(pixie), 14);
	EXPECT_EQ(gameState()->getCreatureBaseGrowth(sprite), 14);

	const auto pixieDwelling = BuildingID::getDwellingFromLevel(0, 0);
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

	// Build the ordinary Pixie dwelling and Garden through the authoritative
	// handler, then recruit a Pixie before upgrading the same stock row.
	const std::array<BuildingID, 4> prerequisiteAndDwellings = {
		BuildingID::FORT,
		BuildingID::MAGES_GUILD_1,
		pixieDwelling,
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
	ASSERT_EQ(town->creatures.at(0).first, 14u);
	EXPECT_EQ(town->creatureGrowth(0), 18);
	const int unrelatedRowGrowth = town->creatureGrowth(2);

	ASSERT_TRUE(gameHandler.recruitCreatures(town->id, town->id, pixie, 1, 0, PlayerColor(0)));
	EXPECT_EQ(town->creatures.at(0).first, 13u);
	auto pixieSlot = town->getSlotFor(pixie);
	ASSERT_TRUE(pixieSlot.validSlot());
	EXPECT_EQ(town->getStackCount(pixieSlot), 1);

	ASSERT_TRUE(gameHandler.buildStructure(town->id, spriteDwelling));
	EXPECT_EQ(town->creatures.at(0).second, (std::vector<CreatureID>{pixie, sprite}));
	EXPECT_EQ(town->creatures.at(0).first, 13u)
		<< "Upgrading the dwelling changes the available creature, not the shared row stock";
	EXPECT_EQ(town->creatureGrowth(0), 18);

#ifdef ENABLE_NULLKILLER2_AI
	// Both building variants resolve to the same explicit row and shared growth.
	auto armyManager = std::make_unique<NK2AI::ArmyManager>(nullptr, nullptr);
	const auto spriteInfo = NK2AI::BuildingInfo(town->getTown()->buildings.at(spriteDwelling).get(),
		sprite.toCreature(), sprite, town, armyManager);
	const auto pixieInfo = NK2AI::BuildingInfo(town->getTown()->buildings.at(pixieDwelling).get(),
		pixie.toCreature(), pixie, town, armyManager);
	EXPECT_TRUE(spriteInfo.isBuilt);
	EXPECT_EQ(spriteInfo.creatureGrowth, town->creatureGrowth(0));
	EXPECT_EQ(spriteInfo.creatureGrowth, 18);
	EXPECT_EQ(pixieInfo.creatureGrowth, town->creatureGrowth(0));
	EXPECT_EQ(pixieInfo.creatureGrowth, 18);
#endif

	for(int day = 1; day < 8; ++day)
		gameHandler.onNewTurn();
	ASSERT_EQ(gameState()->day, 8);
	EXPECT_EQ(town->creatures.at(0).first, 31u);
	EXPECT_EQ(town->creatureGrowth(2), unrelatedRowGrowth);

	const auto townId = town->id;
	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredTown = restored.getTown(townId);
	ASSERT_NE(restoredTown, nullptr);
	EXPECT_EQ(restoredTown->getTown()->creatures.size(), 7u);
	ASSERT_EQ(restoredTown->creatures.size(), 7u);
	EXPECT_EQ(restoredTown->creatures.at(0).second, (std::vector<CreatureID>{pixie, sprite}));
	EXPECT_EQ(restoredTown->creatures.at(0).first, 31u);
	EXPECT_EQ(restored.getCreatureBaseGrowth(pixie), 14);
	EXPECT_EQ(restored.getCreatureBaseGrowth(sprite), 14);
	EXPECT_EQ(restoredTown->creatureGrowth(0), 18);

	ASSERT_TRUE(gameHandler.recruitCreatures(town->id, town->id, sprite, 1, 0, PlayerColor(0)));
	EXPECT_EQ(town->creatures.at(0).first, 30u);
	auto spriteSlot = town->getSlotFor(sprite);
	ASSERT_TRUE(spriteSlot.validSlot());
	EXPECT_EQ(town->getStackCount(spriteSlot), 1);
}

TEST_F(NewHorizonsConfluxGrowthTest, NewConfluxTownStartsWithSevenRowsAndPreservesOldElementalIdentities)
{
	const auto pixie = creature("core:pixie");
	const auto sprite = creature("core:sprite");
	const auto wisp = creature("new-horizons:wisp");
	const auto greaterWisp = creature("new-horizons:wispUpgrade");
	const auto psychic = creature("core:psychicElemental");
	const auto magic = creature("core:magicElemental");
	const auto firebird = creature("core:firebird");
	startConfluxMap();
	const auto * town = expectAt<CGTownInstance>({18, 18, 0});
	ASSERT_NE(town, nullptr);
	EXPECT_EQ(town->getTown()->creatures.size(), 7u);
	EXPECT_EQ(town->creatures.size(), 7u);
	EXPECT_EQ(town->getTown()->creatures.at(0), (std::vector<CreatureID>{pixie, sprite}));
	EXPECT_EQ(town->getTown()->creatures.at(5), (std::vector<CreatureID>{wisp, greaterWisp}));
	EXPECT_EQ(town->getTown()->creatures.at(6).front(), firebird);
	EXPECT_EQ(town->creatures.at(0).second.size(), 0u);
	EXPECT_EQ(town->creatures.at(5).second.size(), 0u);
	ASSERT_NE(psychic.toCreature(), nullptr);
	ASSERT_NE(magic.toCreature(), nullptr);
	EXPECT_EQ(gameState()->getCreatureBaseGrowth(psychic), 3);
	EXPECT_EQ(gameState()->getCreatureBaseGrowth(magic), 3);
}

TEST_F(NewHorizonsConfluxGrowthTest, WispUpgradeSharesRowGrowthSaveAndRecruitment)
{
	const auto wisp = creature("new-horizons:wisp");
	const auto greaterWisp = creature("new-horizons:wispUpgrade");
	const auto psychic = creature("core:psychicElemental");
	const auto magic = creature("core:magicElemental");
	const auto wispDwelling = BuildingID::getDwellingFromLevel(5, 0);
	const auto upgradedWispDwelling = BuildingID::getDwellingFromLevel(5, 1);
	startConfluxMap();
	auto * town = expectAt<CGTownInstance>({18, 18, 0});
	ASSERT_NE(town, nullptr);
	ASSERT_EQ(town->getTown()->creatures.size(), 7u);
	ASSERT_EQ(town->creatures.size(), 7u);
	ASSERT_EQ(town->getTown()->creatures.at(5), (std::vector<CreatureID>{wisp, greaterWisp}));
	ASSERT_NE(wisp.toCreature(), nullptr);
	ASSERT_NE(greaterWisp.toCreature(), nullptr);
	ASSERT_NE(psychic.toCreature(), nullptr);
	ASSERT_NE(magic.toCreature(), nullptr);
	EXPECT_EQ(gameState()->getCreatureBaseGrowth(wisp), 8);
	EXPECT_EQ(gameState()->getCreatureBaseGrowth(greaterWisp), 8);
	EXPECT_TRUE(town->getTown()->buildings.contains(wispDwelling));
	EXPECT_TRUE(town->getTown()->buildings.contains(upgradedWispDwelling));

	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler gameHandler(server, gameState());
	gameHandler.randomizer->setSeed(293);
	grantBuildingResources(*this);
	gameHandler.onNewTurn();
	ASSERT_EQ(gameState()->day, 1);

	const std::array<BuildingID, 8> prerequisites = {
		BuildingID::FORT,
		BuildingID::MAGES_GUILD_1,
		BuildingID::DWELL_LVL_1,
		BuildingID::DWELL_LVL_2,
		BuildingID::DWELL_LVL_3,
		BuildingID::DWELL_LVL_4,
		BuildingID::DWELL_LVL_5,
		wispDwelling
	};
	for(const auto building : prerequisites)
	{
		if(town->hasBuilt(building))
			continue;
		ASSERT_TRUE(gameHandler.buildStructure(town->id, building)) << "failed building " << building;
	}

	ASSERT_EQ(town->creatures.at(5).second, (std::vector<CreatureID>{wisp}));
	ASSERT_EQ(town->creatures.at(5).first, 8u);
	EXPECT_EQ(town->creatureGrowth(5), 8);
	ASSERT_TRUE(gameHandler.recruitCreatures(town->id, town->id, wisp, 1, 5, PlayerColor(0)));
	EXPECT_EQ(town->creatures.at(5).first, 7u);
	auto wispSlot = town->getSlotFor(wisp);
	ASSERT_TRUE(wispSlot.validSlot());
	EXPECT_EQ(town->getStackCount(wispSlot), 1);

	ASSERT_TRUE(gameHandler.buildStructure(town->id, BuildingID::MAGES_GUILD_2));
	ASSERT_TRUE(gameHandler.buildStructure(town->id, upgradedWispDwelling));
	EXPECT_EQ(town->creatures.at(5).second, (std::vector<CreatureID>{wisp, greaterWisp}));
	EXPECT_EQ(town->creatureGrowth(5), 8);

	for(int day = 1; day < 8; ++day)
		gameHandler.onNewTurn();
	ASSERT_EQ(gameState()->day, 8);
	EXPECT_EQ(town->creatures.at(5).first, 15u);

	const auto townId = town->id;
	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredTown = restored.getTown(townId);
	ASSERT_NE(restoredTown, nullptr);
	EXPECT_EQ(restoredTown->getTown()->creatures.size(), 7u);
	ASSERT_EQ(restoredTown->creatures.size(), 7u);
	EXPECT_EQ(restoredTown->creatures.at(5).second, (std::vector<CreatureID>{wisp, greaterWisp}));
	EXPECT_EQ(restoredTown->creatures.at(5).first, 15u);
	EXPECT_EQ(restoredTown->creatureGrowth(5), 8);
	EXPECT_EQ(restored.getCreatureBaseGrowth(wisp), 8);
	EXPECT_EQ(restored.getCreatureBaseGrowth(greaterWisp), 8);

	ASSERT_TRUE(gameHandler.recruitCreatures(town->id, town->id, greaterWisp, 1, 5, PlayerColor(0)));
	EXPECT_EQ(town->creatures.at(5).first, 14u);
	auto greaterWispSlot = town->getSlotFor(greaterWisp);
	ASSERT_TRUE(greaterWispSlot.validSlot());
	EXPECT_EQ(town->getStackCount(greaterWispSlot), 1);
	EXPECT_EQ(gameState()->getCreatureBaseGrowth(psychic), 3);
	EXPECT_EQ(gameState()->getCreatureBaseGrowth(magic), 3);
}

TEST_F(NewHorizonsConfluxGrowthTest, VaultOfAshesAddsFireLineGrowthAtTheWeeklyBoundary)
{
	const auto fireElemental = creature("core:fireElemental");
	const auto energyElemental = creature("core:energyElemental");
	const auto waterElemental = creature("core:waterElemental");
	startConfluxMap();
	auto * town = expectAt<CGTownInstance>({18, 18, 0});
	ASSERT_NE(town, nullptr);
	ASSERT_EQ(town->getTown()->creatures.size(), 7u);
	ASSERT_EQ(town->creatures.size(), 7u);
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
	ASSERT_EQ(restoredTown->creatures.size(), 7u);
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
