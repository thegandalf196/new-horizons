/*
 * NewHorizonsNeutralArmyWorldTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../mock/GameHandlerTestServer.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/GameSettings.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/mapObjects/CGDwelling.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGResource.h"
#include "../../lib/mapObjects/CRewardableObject.h"
#include "../../lib/mapObjects/CGPandoraBox.h"
#include "../../lib/mapObjects/NewHorizonsNeutralArmies.h"
#include "../../lib/mapObjectConstructors/CObjectClassesHandler.h"
#include "../../lib/mapObjectConstructors/CRewardableConstructor.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../server/CGameHandler.h"
#include "../../server/processors/NewTurnProcessor.h"

namespace
{
constexpr auto option = EGameSettings::CREATURES_NEUTRAL_ARMY_SIZE_PERCENT;
CreatureID troop() { return CreatureID(CreatureID::decode("core:pikeman")); }

template<typename Value>
auto bytes(const Value & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}

template<typename Value>
void rejectsOldPrefix(const Value & value)
{
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::NEW_HORIZONS_SORCERY_REVISION;
	try
	{
		memory.oser & value;
		FAIL() << "Expected neutral-size compatibility rejection";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_NE(std::string(error.what()).find("Reduced neutral army size"), std::string::npos);
	}
	EXPECT_TRUE(memory.extractBuffer().empty());
}

std::shared_ptr<CGameState> restoredWorld(std::vector<std::byte> saved)
{
	auto result = std::make_shared<CGameState>();
	result->preInit(LIBRARY);
	result->loadFromMemory(std::move(saved));
	return result;
}

class NewHorizonsNeutralArmyWorldTest : public TinyMapGameTest
{
protected:
	int percent = 50;
	Services * gameServices() override { return LIBRARY; }
	void configureWorldInitialization(StartInfo &, GameRandomizer & rng) const override { rng.setSeed(1337); }
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(option, JsonNode(percent));
		for(auto * pile : loaded->getObjects<CGResource>())
			for(int slot = 0; slot < 7; ++slot)
				pile->putStack(SlotID(slot), std::make_unique<CStackInstance>(gameState().get(), troop(), slot ? 6 : 7));
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0)).heroGarrison({{troop(), 7}})
			.hero({8, 8, 0}, HeroTypeID(1), PlayerColor(1)).heroGarrison({{troop(), 7}})
			.monster({12, 6, 0}, troop(), 43)
			.resource({16, 10, 0}, GameResID(EGameResID::WOOD), 1)
			.town({25, 25, 0}, FactionID::CASTLE, PlayerColor::NEUTRAL).townGarrison({{troop(), 7}})
			.dwelling({8, 17, 0}, MapObjectSubID(56), PlayerColor::NEUTRAL);
		startWithMap(std::move(builder));
	}
	template<typename Army>
	std::shared_ptr<Army> initialArmy(CMap & isolated, PlayerColor owner, int count)
	{
		auto army = std::make_shared<Army>(gameState().get());
		army->tempOwner = owner;
		army->putStack(SlotID(0), std::make_unique<CStackInstance>(gameState().get(), troop(), count));
		isolated.objects.push_back(army);
		return army;
	}
};
}

TEST_F(NewHorizonsNeutralArmyWorldTest, FreshWorldScalesAuthoredMonstersAndUnflaggableResourceGuardsOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * monster = findFirst<CGCreature>();
	const auto * pile = findFirst<CGResource>();
	ASSERT_NE(monster, nullptr);
	ASSERT_NE(pile, nullptr);
	EXPECT_EQ(monster->getStackCount(SlotID(0)), 22);
	EXPECT_EQ(monster->temppower, 22000);
	EXPECT_EQ(pile->getOwner(), PlayerColor::UNFLAGGABLE);
	EXPECT_EQ(pile->stacksCount(), 7);
	EXPECT_EQ(pile->getStackCount(SlotID(0)), 4);
	for(int slot = 1; slot < 7; ++slot)
		EXPECT_EQ(pile->getStackCount(SlotID(slot)), 3);
	for(const auto * hero : map()->getObjects<CGHeroInstance>())
		EXPECT_EQ(hero->getStackCount(SlotID(0)), 7);
	EXPECT_EQ(findFirst<CGTownInstance>()->getStackCount(SlotID(0)), 4);
	EXPECT_EQ(newHorizonsNeutralArmies::percentage(gameState()->getSettings()), 50);
}

TEST_F(NewHorizonsNeutralArmyWorldTest, IdentityWorldPreservesAuthoredAmounts)
{
	percent = 100;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(findFirst<CGCreature>()->getStackCount(SlotID(0)), 43);
	EXPECT_EQ(findFirst<CGCreature>()->temppower, 43000);
	EXPECT_EQ(findFirst<CGResource>()->getStackCount(SlotID(0)), 7);
	EXPECT_EQ(findFirst<CGTownInstance>()->getStackCount(SlotID(0)), 7);
	EXPECT_EQ(newHorizonsNeutralArmies::percentage(gameState()->getSettings()), 100);
}

TEST_F(NewHorizonsNeutralArmyWorldTest, InitialTaxonomyPreservesPlayerPrisonerRewardsAndRecruitment)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CMap isolated(gameState().get());
	isolated.overrideGameSetting(option, JsonNode(50));
	auto artifact = initialArmy<CGArtifact>(isolated, PlayerColor::UNFLAGGABLE, 7);
	auto mine = initialArmy<CGMine>(isolated, PlayerColor::NEUTRAL, 7);
	auto garrison = initialArmy<CGGarrison>(isolated, PlayerColor::NEUTRAL, 7);
	auto owned = initialArmy<CGGarrison>(isolated, PlayerColor(0), 7);
	auto prisoner = initialArmy<CGHeroInstance>(isolated, PlayerColor::NEUTRAL, 7);
	auto rewardable = initialArmy<CRewardableObject>(isolated, PlayerColor::UNFLAGGABLE, 7);
	auto pandora = initialArmy<CGPandoraBox>(isolated, PlayerColor::UNFLAGGABLE, 7);
	auto dwelling = initialArmy<CGDwelling>(isolated, PlayerColor::NEUTRAL, 7);
	dwelling->creatures = {{17, {troop()}}};
	rewardable->configuration.info.emplace_back();
	auto & reward = rewardable->configuration.info.front().reward;
	reward.guards.emplace_back(troop(), 7);
	reward.creatures.emplace_back(troop(), 17);
	const auto rngBefore = bytes(dynamic_cast<CRandomGenerator &>(*getRNG()));
	newHorizonsNeutralArmies::initializeWorld(isolated);
	const std::array<const CArmedInstance *, 6> scaledArmies = {
		artifact.get(), mine.get(), garrison.get(), rewardable.get(), pandora.get(), dwelling.get()};
	for(const auto * army : scaledArmies)
		EXPECT_EQ(army->getStackCount(SlotID(0)), 4);
	EXPECT_EQ(owned->getStackCount(SlotID(0)), 7);
	EXPECT_EQ(prisoner->getStackCount(SlotID(0)), 7);
	EXPECT_EQ(reward.guards.front().getCount(), 4);
	EXPECT_EQ(reward.creatures.front().getCount(), 17);
	EXPECT_EQ(dwelling->creatures.front().first, 17u);
	EXPECT_EQ(bytes(dynamic_cast<CRandomGenerator &>(*getRNG())), rngBefore);
}

TEST_F(NewHorizonsNeutralArmyWorldTest, SaveReloadResaveDoesNotHalveAgainAndOldEnvelopesRejectBeforePrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	rejectsOldPrefix(dynamic_cast<const GameSettings &>(map()->getSettings()));
	rejectsOldPrefix(*map());
	rejectsOldPrefix(*gameState());
	LobbyStartGame start;
	start.initializedGameState = gameState();
	rejectsOldPrefix(start);
	auto restored = restoredWorld(gameState()->saveToMemory());
	EXPECT_EQ(newHorizonsNeutralArmies::percentage(restored->getSettings()), 50);
	const auto * monster = restored->getMap().getObjects<CGCreature>().front();
	EXPECT_EQ(monster->getStackCount(SlotID(0)), 22);
	EXPECT_EQ(monster->temppower, 22000);
	auto reread = restoredWorld(restored->saveToMemory());
	EXPECT_EQ(reread->getMap().getObjects<CGCreature>().front()->getStackCount(SlotID(0)), 22);
	EXPECT_EQ(reread->getMap().getObjects<CGResource>().front()->getStackCount(SlotID(0)), 4);
}

TEST(NewHorizonsNeutralArmySerialization, DefaultWorldAndUninitializedLobbyWritersAcceptOldAndCurrentFormats)
{
	for(const auto version : {ESerializationVersion::NEW_HORIZONS_SORCERY_REVISION, ESerializationVersion::CURRENT})
	{
		CGameState empty;
		EXPECT_NO_THROW(empty.validateNeutralArmySizeSerialization(false));
		CMemorySerializer worldBytes;
		worldBytes.oser.version = version;
		EXPECT_NO_THROW(worldBytes.oser & empty);
		EXPECT_FALSE(worldBytes.extractBuffer().empty());

		LobbyStartGame nullLobby;
		CMemorySerializer nullBytes;
		nullBytes.oser.version = version;
		EXPECT_NO_THROW(nullBytes.oser & nullLobby);

		LobbyStartGame uninitialized;
		uninitialized.initializedGameState = std::make_shared<CGameState>();
		EXPECT_NO_THROW(uninitialized.validateNeutralArmySizeSerialization(false));
		CMemorySerializer lobbyBytes;
		lobbyBytes.oser.version = version;
		EXPECT_NO_THROW(lobbyBytes.oser & uninitialized);
		EXPECT_FALSE(lobbyBytes.extractBuffer().empty());
		// Reduced initialized captures are separately rejected before any prefix.
		// Empty writers have no captured policy to reject or normalize.
		EXPECT_EQ(uninitialized.initializedStartInfo, nullptr);
	}
}

TEST_F(NewHorizonsNeutralArmyWorldTest, FirstTwoGrowthBoundariesUseScaledFractionalPopulationAfterReload)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	gameState()->day = 7;
	auto restored = restoredWorld(gameState()->saveToMemory());
	GameHandlerTestServer server(restored);
	CGameHandler handler(server, restored);
	auto * monster = restored->getMap().getObjects<CGCreature>().front();
	const auto rngBefore = bytes(*handler.randomizer);
	restored->day = 8;
	monster->newTurn(handler, *handler.randomizer);
	EXPECT_EQ(monster->getStackCount(SlotID(0)), 24);
	EXPECT_EQ(monster->temppower, 24200);
	restored->day = 15;
	monster->newTurn(handler, *handler.randomizer);
	EXPECT_EQ(monster->getStackCount(SlotID(0)), 26);
	EXPECT_EQ(monster->temppower, 26620);
	EXPECT_EQ(bytes(*handler.randomizer), rngBefore);
}

TEST_F(NewHorizonsNeutralArmyWorldTest, DwellingWeeklyGuardRebuildDoesNotCompoundOrAlterRecruitStock)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * dwelling = findFirst<CGDwelling>();
	ASSERT_NE(dwelling, nullptr);
	const CreatureID guard(CreatureID::decode("core:angel"));
	dwelling->creatures = {{17, {guard}}};
	dwelling->clearSlots();
	GameHandlerTestServer server(gameState());
	CGameHandler handler(server, gameState());
	const int raw = gameState()->getCreatureBaseGrowth(guard) * 3;
	const int scaled = newHorizonsNeutralArmies::scaleCount(raw, 50);
	gameState()->day = 8;
	static_cast<const IObjectInterface *>(dwelling)->newTurn(handler, *handler.randomizer);
	EXPECT_EQ(dwelling->getStackCount(SlotID(0)), scaled);
	const auto firstStock = dwelling->creatures;
	gameState()->day = 15;
	static_cast<const IObjectInterface *>(dwelling)->newTurn(handler, *handler.randomizer);
	EXPECT_EQ(dwelling->getStackCount(SlotID(0)), scaled);
	EXPECT_EQ(dwelling->creatures, firstStock);
	dwelling->tempOwner = PlayerColor(0);
	gameState()->day = 22;
	static_cast<const IObjectInterface *>(dwelling)->newTurn(handler, *handler.randomizer);
	EXPECT_EQ(dwelling->getStackCount(SlotID(0)), raw);
}

TEST_F(NewHorizonsNeutralArmyWorldTest, RuntimeAuthoredMonsterCreationKeepsExplicitCountAndPower)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	GameHandlerTestServer server(gameState());
	CGameHandler handler(server, gameState());
	handler.createWanderingMonster({20, 10, 0}, troop(), 43);
	const auto * created = dynamic_cast<const CGCreature *>(findObjectAt({20, 10, 0}));
	// Monster templates may offset their anchor; identify the new instance instead.
	if(!created)
		created = map()->getObjects<CGCreature>().back();
	ASSERT_NE(created, nullptr);
	EXPECT_EQ(created->getStackCount(SlotID(0)), 43);
	EXPECT_EQ(created->temppower, 43000);
}

TEST_F(NewHorizonsNeutralArmyWorldTest, RegeneratedBankGuardsScaleRawConfigurationOnceAndPreserveRewardsAndRng)
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
	for(int round = 0; round < 2; ++round)
	{
		bank->configuration.resetParameters.days = 1;
		bank->configuration.resetParameters.rewards = true;
		gameState()->day = round + 2;
		GameRandomizer reference(*gameState());
		reference.setSeed(1337 + round);
		handler.randomizer->setSeed(1337 + round);
		auto expected = constructor->generateConfiguration(bank->cb, reference, bank->ID, bank->configuration.variables.preset);
		for(auto & visit : expected.info)
			for(auto & guard : visit.reward.guards)
				guard.setCount((guard.getCount() + 1) / 2);
		bank->newTurn(handler, *handler.randomizer);
		ASSERT_EQ(bank->configuration.info.size(), expected.info.size());
		for(size_t i = 0; i < expected.info.size(); ++i)
		{
			EXPECT_EQ(bytes(bank->configuration.info[i].reward), bytes(expected.info[i].reward));
		}
		EXPECT_EQ(bytes(*handler.randomizer), bytes(reference));
		int count = 0;
		for(const auto & info : expected.info)
			for(const auto & stack : info.reward.guards)
				EXPECT_EQ(bank->getStackCount(SlotID(count++)), stack.getCount());
		EXPECT_GT(count, 0);
	}
}

TEST_F(NewHorizonsNeutralArmyWorldTest, NaturalPopulationUsesSameTilesSpeciesAndRngWithOnlyNewCountsReduced)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto saved = gameState()->saveToMemory();
	auto whole = restoredWorld(saved);
	auto half = restoredWorld(saved);
	whole->getMap().overrideGameSetting(option, JsonNode(100));
	GameHandlerTestServer wholeServer(whole), halfServer(half);
	CGameHandler wholeHandler(wholeServer, whole), halfHandler(halfServer, half);
	wholeHandler.randomizer->setSeed(2121);
	halfHandler.randomizer->setSeed(2121);
	const auto before = whole->getMap().getObjects<CGCreature>().size();
	wholeHandler.spawnWanderingMonsters(troop());
	halfHandler.spawnWanderingMonsters(troop());
	const auto fullMonsters = whole->getMap().getObjects<CGCreature>();
	const auto halfMonsters = half->getMap().getObjects<CGCreature>();
	ASSERT_EQ(fullMonsters.size(), halfMonsters.size());
	ASSERT_GT(fullMonsters.size(), before);
	for(size_t i = before; i < fullMonsters.size(); ++i)
	{
		EXPECT_EQ(fullMonsters[i]->anchorPos(), halfMonsters[i]->anchorPos());
		EXPECT_EQ(fullMonsters[i]->getStack(SlotID(0)).getCreatureID(), halfMonsters[i]->getStack(SlotID(0)).getCreatureID());
		const int raw = fullMonsters[i]->getStackCount(SlotID(0));
		EXPECT_EQ(halfMonsters[i]->getStackCount(SlotID(0)), (raw + 1) / 2);
		EXPECT_EQ(halfMonsters[i]->temppower, ((raw + 1) / 2) * 1000);
	}
	EXPECT_EQ(bytes(*wholeHandler.randomizer), bytes(*halfHandler.randomizer));
}

TEST_F(NewHorizonsNeutralArmyWorldTest, NaturalTownReinforcementsScaleOnlyNewGrowthAndKeepRecruitmentAndRng)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	gameState()->day = 7;
	const auto saved = gameState()->saveToMemory();
	int exercisedGrowth = 0;
	for(int seed = 1; seed <= 12; ++seed)
	{
		auto whole = restoredWorld(saved);
		auto half = restoredWorld(saved);
		whole->getMap().overrideGameSetting(option, JsonNode(100));
		GameHandlerTestServer wholeServer(whole), halfServer(half);
		CGameHandler wholeHandler(wholeServer, whole), halfHandler(halfServer, half);
		wholeHandler.randomizer->setSeed(seed);
		halfHandler.randomizer->setSeed(seed);
		NewTurnProcessor(&wholeHandler).onNewTurn();
		NewTurnProcessor(&halfHandler).onNewTurn();
		const auto * fullTown = whole->getMap().getObjects<CGTownInstance>().front();
		const auto * halfTown = half->getMap().getObjects<CGTownInstance>().front();
		ASSERT_EQ(fullTown->stacksCount(), halfTown->stacksCount());
		EXPECT_EQ(fullTown->creatures, halfTown->creatures);
		for(const auto & [slot, stack] : fullTown->Slots())
		{
			EXPECT_EQ(stack->getCreatureID(), halfTown->getStack(slot).getCreatureID());
			const int oldCount = slot == SlotID(0) ? 4 : 0;
			const int growth = stack->getCount() - oldCount;
			EXPECT_GE(growth, 0);
			EXPECT_EQ(halfTown->getStackCount(slot), oldCount + (growth + 1) / 2);
			exercisedGrowth += growth > 0;
		}
		EXPECT_EQ(bytes(*wholeHandler.randomizer), bytes(*halfHandler.randomizer));
	}
	EXPECT_GT(exercisedGrowth, 0);
}
