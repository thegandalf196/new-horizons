/*
 * NewHorizonsPortalSummoningTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"

#include <gtest/gtest.h>

#include "battles/FullGameSnapshotTypes.h"

#include "../../lib/GameConstants.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/ResourceSet.h"
#include "../../lib/mapObjects/CGDwelling.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/ESerializationVersion.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../server/CGameHandler.h"
#include "../../server/queries/MapQueries.h"
#include "../../server/queries/QueriesProcessor.h"
#include "../../server/queries/VisitQueries.h"
#include "../mock/GameHandlerTestServer.h"
#include "../mock/TinyH3MBuilder.h"
#include "../mock/TinyMapGameTest.h"

namespace
{
const PlayerColor PLAYER(0);
const PlayerColor OTHER_PLAYER(1);

CreatureID creature(const char * id)
{
	return CreatureID(CreatureID::decode(id));
}

class NewHorizonsPortalSummoningTest : public TinyMapGameTest
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
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
	}

	void startGame()
	{
		const FactionID dungeon(FactionID::decode("core:dungeon"));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER)
			.town({12, 12, 0}, dungeon, PLAYER)
			.town({12, 24, 0}, dungeon, PLAYER)
			.hero({4, 7, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PLAYER)
			.hero({4, 25, 0}, HeroTypeID(HeroTypeID::decode("core:valeska")), PLAYER)
			// Subtype 56 is the core Pikeman external dwelling used by the
			// existing Recruitment Muster server fixture.
			.dwelling({8, 8, 0}, MapObjectSubID(56), PLAYER)
			.dwelling({8, 17, 0}, MapObjectSubID(56), PLAYER);
		startWithMap(std::move(builder));

		town = expectAt<CGTownInstance>({12, 12, 0});
		town2 = expectAt<CGTownInstance>({12, 24, 0});
		hero = findHeroAt({4, 7, 0});
		remoteHero = findHeroAt({4, 25, 0});
		source = expectAt<CGDwelling>({8, 8, 0});
		source2 = expectAt<CGDwelling>({8, 17, 0});
		ASSERT_NE(town, nullptr);
		ASSERT_NE(town2, nullptr);
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(remoteHero, nullptr);
		ASSERT_NE(source, nullptr);
		ASSERT_NE(source2, nullptr);
		ASSERT_EQ(source->ID, Obj::CREATURE_GENERATOR1);
		ASSERT_EQ(source2->ID, Obj::CREATURE_GENERATOR1);

		// Dungeon special3 is the faction's Portal of Summoning.
		town->addBuilding(BuildingID::SPECIAL_3);
		town2->addBuilding(BuildingID::SPECIAL_3);
		ASSERT_TRUE(town->hasBuilt(BuildingSubID::PORTAL_OF_SUMMONING));
		ASSERT_TRUE(town2->hasBuilt(BuildingSubID::PORTAL_OF_SUMMONING));

		griffin = creature("core:griffin");
		pikeman = creature("core:pikeman");
		source->creatures = {{20, {griffin}}};
		source2->creatures = {{20, {pikeman}}};
		gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD] = 100000;
	}

	CGTownInstance * town = nullptr;
	CGTownInstance * town2 = nullptr;
	CGHeroInstance * hero = nullptr;
	CGHeroInstance * remoteHero = nullptr;
	CGDwelling * source = nullptr;
	CGDwelling * source2 = nullptr;
	CreatureID griffin = CreatureID::NONE;
	CreatureID pikeman = CreatureID::NONE;
};
}

TEST_F(NewHorizonsPortalSummoningTest, SourceSelectionIsPerTownWeeklyAndNeverCopiesPortalGrowth)
{
	startGame();
	ASSERT_TRUE(newHorizonsMagic::rulesActive(gameState()->getMagicRules()));
	const auto sourceStock = source->creatures;
	const auto secondSourceStock = source2->creatures;
	const auto firstTownRows = town->creatures;
	const auto secondTownRows = town2->creatures;
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());

	// The same legacy weekly entry point must not create an independent bonus pool.
	handler.setPortalDwelling(town, true, false);
	EXPECT_EQ(town->creatures, firstTownRows);
	ASSERT_TRUE(handler.selectPortalDwelling(town->id, source->id, PLAYER));
	EXPECT_EQ(town->portalSourceDwellingId, source->id);
	EXPECT_EQ(town->portalLastSelectionWeek, 0);
	EXPECT_EQ(source->creatures, sourceStock);
	EXPECT_EQ(town->creatures, firstTownRows);

	// Each built Portal town gets its own weekly selection marker, even when both
	// towns select the same shared external dwelling.
	ASSERT_TRUE(handler.selectPortalDwelling(town2->id, source->id, PLAYER));
	EXPECT_EQ(town2->portalSourceDwellingId, source->id);
	EXPECT_EQ(town2->portalLastSelectionWeek, 0);
	EXPECT_EQ(source->creatures, sourceStock);
	EXPECT_EQ(source2->creatures, secondSourceStock);
	EXPECT_EQ(town2->creatures, secondTownRows);

	EXPECT_FALSE(handler.selectPortalDwelling(town->id, source2->id, PLAYER));
	EXPECT_EQ(town->portalSourceDwellingId, source->id);
	EXPECT_EQ(town->portalLastSelectionWeek, 0);

	// Rollover leaves the current source linked until a legal replacement request.
	gameState()->day = 8;
	EXPECT_EQ(town->portalSourceDwellingId, source->id);
	ASSERT_TRUE(handler.selectPortalDwelling(town->id, source2->id, PLAYER));
	EXPECT_EQ(town->portalSourceDwellingId, source2->id);
	EXPECT_EQ(town->portalLastSelectionWeek, 1);
	EXPECT_EQ(source2->creatures, secondSourceStock);
	EXPECT_EQ(town->creatures, firstTownRows);
}

TEST_F(NewHorizonsPortalSummoningTest, PortalAndVisitedDwellingDeductTheSameRealStockAndCost)
{
	startGame();
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	ASSERT_TRUE(handler.selectPortalDwelling(town->id, source->id, PLAYER));
	const auto townRows = town->creatures;
	const auto unitCost = source->getRecruitmentCost(griffin);
	ASSERT_GT(unitCost[EGameResID::GOLD], 0);

	const auto resourcesBefore = gameState()->getPlayerState(PLAYER)->resources;
	ASSERT_TRUE(handler.recruitCreatures(source->id, town->id, griffin, 3, 0, PLAYER, town->id));
	EXPECT_EQ(source->creatures.front().first, 17u);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources, resourcesBefore - unitCost * 3);
	const auto townSlot = town->getSlotFor(griffin);
	ASSERT_TRUE(townSlot.validSlot());
	EXPECT_EQ(town->getStackCount(townSlot), 3);

	// A normal map-object visit still purchases from that exact dwelling's
	// remaining row, not from a town-side copy.
	hero->clearSlots();
	handler.queries->addQuery(std::make_shared<MapObjectVisitQuery>(&handler, source, hero));
	ASSERT_EQ(handler.getVisitingObject(hero), source);
	const auto afterPortalResources = gameState()->getPlayerState(PLAYER)->resources;
	ASSERT_TRUE(handler.recruitCreatures(source->id, hero->id, griffin, 1, 0, PLAYER));
	EXPECT_EQ(source->creatures.front().first, 16u);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources, afterPortalResources - unitCost);
	const auto heroSlot = hero->getSlotFor(griffin);
	ASSERT_TRUE(heroSlot.validSlot());
	EXPECT_EQ(hero->getStackCount(heroSlot), 1);
	EXPECT_EQ(town->creatures, townRows);
}

TEST_F(NewHorizonsPortalSummoningTest, ForgedContextAndLostOwnershipRejectWithoutMutation)
{
	startGame();
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	ASSERT_TRUE(handler.selectPortalDwelling(town->id, source->id, PLAYER));
	const auto stock = source->creatures;
	const auto resources = gameState()->getPlayerState(PLAYER)->resources;
	const auto townSlot = town->getSlotFor(griffin);
	ASSERT_TRUE(townSlot.validSlot());
	const auto unchanged = [&]
	{
		EXPECT_EQ(source->creatures, stock);
		EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources, resources);
		EXPECT_EQ(town->getStackCount(townSlot), 0);
		EXPECT_EQ(town->portalSourceDwellingId, source->id);
		EXPECT_EQ(town->portalLastSelectionWeek, 0);
	};

	// A different dwelling, a different Portal town, or omitted context must
	// never turn source ownership into general remote-recruit authority.
	EXPECT_FALSE(handler.recruitCreatures(source2->id, town->id, pikeman, 1, 0, PLAYER, town->id));
	unchanged();
	EXPECT_FALSE(handler.recruitCreatures(source->id, town2->id, griffin, 1, 0, PLAYER));
	unchanged();
	EXPECT_FALSE(handler.recruitCreatures(source->id, town->id, griffin, 1, 0, PLAYER, town2->id));
	unchanged();
	EXPECT_FALSE(handler.recruitCreatures(source->id, remoteHero->id, griffin, 1, 0, PLAYER, town->id));
	unchanged();

	// The linked source is checked again at the point of use, including after
	// ownership changes since selection.
	source->tempOwner = OTHER_PLAYER;
	EXPECT_FALSE(handler.recruitCreatures(source->id, town->id, griffin, 1, 0, PLAYER, town->id));
	source->tempOwner = PLAYER;
	unchanged();
}

TEST_F(NewHorizonsPortalSummoningTest, InsufficientFundsAndLeadershipRemainAtomic)
{
	startGame();
	town->setVisitingHero(hero);
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	ASSERT_TRUE(handler.selectPortalDwelling(town->id, source->id, PLAYER));
	const auto unitCost = source->getRecruitmentCost(griffin);
	ASSERT_GT(unitCost[EGameResID::GOLD], 0);

	auto * playerState = gameState()->getPlayerState(PLAYER);
	playerState->resources = unitCost;
	playerState->resources[EGameResID::GOLD] = unitCost[EGameResID::GOLD] - 1;
	const auto poorResources = playerState->resources;
	const auto initialStock = source->creatures;
	EXPECT_FALSE(handler.recruitCreatures(source->id, town->id, griffin, 1, 0, PLAYER, town->id));
	EXPECT_EQ(source->creatures, initialStock);
	EXPECT_EQ(playerState->resources, poorResources);

	ResourceSet sufficientResources = unitCost * 2;
	playerState->resources = sufficientResources;
	hero->clearSlots();
	const auto capacity = hero->getLeadershipSlotCapacity(griffin);
	ASSERT_TRUE(capacity);
	ASSERT_GT(capacity->maximum, 0);
	ASSERT_TRUE(hero->setCreature(SlotID(0), griffin, capacity->maximum));
	const auto fullArmyResources = playerState->resources;
	EXPECT_FALSE(handler.recruitCreatures(source->id, hero->id, griffin, 1, 0, PLAYER, town->id));
	EXPECT_EQ(source->creatures, initialStock);
	EXPECT_EQ(playerState->resources, fullArmyResources);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), capacity->maximum);
}

TEST_F(NewHorizonsPortalSummoningTest, SelectedLinkAndWireContextRoundTripWithLegacyDefaultsAndLossGuards)
{
	startGame();
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	ASSERT_TRUE(handler.selectPortalDwelling(town->id, source->id, PLAYER));
	gameState()->day = 8;
	ASSERT_TRUE(handler.selectPortalDwelling(town2->id, source2->id, PLAYER));

	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	ASSERT_NE(restored.getTown(town->id), nullptr);
	ASSERT_NE(restored.getTown(town2->id), nullptr);
	EXPECT_EQ(restored.getTown(town->id)->portalSourceDwellingId, source->id);
	EXPECT_EQ(restored.getTown(town->id)->portalLastSelectionWeek, 0);
	EXPECT_EQ(restored.getTown(town2->id)->portalSourceDwellingId, source2->id);
	EXPECT_EQ(restored.getTown(town2->id)->portalLastSelectionWeek, 1);

	SelectPortalDwelling selection;
	selection.townId = town->id;
	selection.sourceDwellingId = source->id;
	SetPortalDwellingSource update;
	update.townId = town->id;
	update.sourceDwellingId = source->id;
	update.lastSelectionWeek = 1;
	RecruitCreatures portalRequest(source->id, town->id, griffin, 2, 0, town->id);
	const auto roundTrip = [](auto & outgoing, auto & incoming)
	{
		CMemorySerializer wire;
		wire.oser.version = ESerializationVersion::CURRENT;
		wire.iser.version = ESerializationVersion::CURRENT;
		wire.oser & outgoing;
		wire.iser & incoming;
	};
	SelectPortalDwelling readSelection;
	SetPortalDwellingSource readUpdate;
	RecruitCreatures readRecruitment;
	roundTrip(selection, readSelection);
	roundTrip(update, readUpdate);
	roundTrip(portalRequest, readRecruitment);
	EXPECT_EQ(readSelection.townId, selection.townId);
	EXPECT_EQ(readSelection.sourceDwellingId, selection.sourceDwellingId);
	EXPECT_EQ(readUpdate.sourceDwellingId, update.sourceDwellingId);
	EXPECT_EQ(readUpdate.lastSelectionWeek, update.lastSelectionWeek);
	EXPECT_EQ(readRecruitment.tid, portalRequest.tid);
	EXPECT_EQ(readRecruitment.dst, portalRequest.dst);
	EXPECT_EQ(readRecruitment.portalTownId, town->id);

	const auto olderVersion = ESerializationVersion::NEW_HORIZONS_INVESTOR_INCOME;
	RecruitCreatures ordinaryRequest(source->id, hero->id, griffin, 1, 0);
	CMemorySerializer legacyRequestWire;
	legacyRequestWire.oser.version = olderVersion;
	legacyRequestWire.iser.version = olderVersion;
	legacyRequestWire.oser & ordinaryRequest;
	RecruitCreatures legacyRequestRead;
	legacyRequestWire.iser & legacyRequestRead;
	EXPECT_EQ(legacyRequestRead.portalTownId, ObjectInstanceID::NONE);

	CMemorySerializer rejectedRecruitment;
	rejectedRecruitment.oser.version = olderVersion;
	EXPECT_THROW(rejectedRecruitment.oser & portalRequest, std::runtime_error);
	EXPECT_TRUE(rejectedRecruitment.extractBuffer().empty());
	CMemorySerializer rejectedSelection;
	rejectedSelection.oser.version = olderVersion;
	EXPECT_THROW(rejectedSelection.oser & selection, std::runtime_error);
	EXPECT_TRUE(rejectedSelection.extractBuffer().empty());
	CMemorySerializer rejectedUpdate;
	rejectedUpdate.oser.version = olderVersion;
	EXPECT_THROW(rejectedUpdate.oser & update, std::runtime_error);
	EXPECT_TRUE(rejectedUpdate.extractBuffer().empty());

	// Older town readers default the appended link state without shifting the
	// following field, while populated town state cannot be silently downsaved.
	const auto selectedTownSource = town->portalSourceDwellingId;
	const auto selectedTownWeek = town->portalLastSelectionWeek;
	town->portalSourceDwellingId = ObjectInstanceID::NONE;
	town->portalLastSelectionWeek = -1;
	CMemorySerializer legacyTownWire;
	legacyTownWire.oser.version = olderVersion;
	const uint32_t sentinel = 0x49504f52;
	legacyTownWire.oser & *town;
	legacyTownWire.oser & sentinel;
	const auto legacyTownBytes = legacyTownWire.extractBuffer();
	town->portalSourceDwellingId = selectedTownSource;
	town->portalLastSelectionWeek = selectedTownWeek;
	CMemorySerializer legacyTownReader(legacyTownBytes);
	legacyTownReader.iser.version = olderVersion;
	legacyTownReader.iser.cb = gameState().get();
	CGTownInstance isolatedTown(gameState().get());
	isolatedTown.portalSourceDwellingId = source2->id;
	isolatedTown.portalLastSelectionWeek = 6;
	legacyTownReader.iser & isolatedTown;
	uint32_t restoredSentinel = 0;
	legacyTownReader.iser & restoredSentinel;
	EXPECT_EQ(restoredSentinel, sentinel);
	EXPECT_EQ(isolatedTown.portalSourceDwellingId, ObjectInstanceID::NONE);
	EXPECT_EQ(isolatedTown.portalLastSelectionWeek, -1);

	town->portalSourceDwellingId = selectedTownSource;
	town->portalLastSelectionWeek = selectedTownWeek;
	CMemorySerializer rejectedTownDownsave;
	rejectedTownDownsave.oser.version = olderVersion;
	EXPECT_THROW(rejectedTownDownsave.oser & *town, std::runtime_error);
	EXPECT_TRUE(rejectedTownDownsave.extractBuffer().empty());
}
