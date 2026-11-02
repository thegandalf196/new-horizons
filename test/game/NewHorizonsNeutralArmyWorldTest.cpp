/*
 * NewHorizonsNeutralArmyWorldTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../mock/GameHandlerTestServer.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/GameSettings.h"
#include "../../lib/bonuses/NativeBonusGraphAccess.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/gameState/QuestInfo.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/mapObjects/CGDwelling.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGResource.h"
#include "../../lib/mapObjects/CRewardableObject.h"
#include "../../lib/mapObjects/CGPandoraBox.h"
#include "../../lib/mapObjects/NewHorizonsNeutralArmies.h"
#include "../../lib/mapObjects/NewHorizonsNeutralCalibration.h"
#include "../../lib/mapObjectConstructors/CObjectClassesHandler.h"
#include "../../lib/mapObjectConstructors/CRewardableConstructor.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/CLoadFile.h"
#include "../../lib/serializer/CSaveFile.h"
#include "../../server/TurnTimerHandler.h"
#include "../../server/battles/BattleProcessor.h"
#include "../../server/processors/HeroPoolProcessor.h"
#include "../../server/processors/PlayerMessageProcessor.h"
#include "../../server/processors/TurnOrderProcessor.h"
#include "../../server/CGameHandler.h"
#include "../../server/processors/NewTurnProcessor.h"
#include <tuple>

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
	bool historicalPerkPolicy = false;
	Services * gameServices() override { return LIBRARY; }
	void configureWorldInitialization(StartInfo &, GameRandomizer & rng) const override { rng.setSeed(1337); }
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		// This fixture characterizes its original unscaled growth/context.
		loaded->overrideGameSetting(EGameSettings::CREATURES_RECRUITMENT_GROWTH_PERCENT, JsonNode(100));
		loaded->overrideGameSetting(option, JsonNode(percent));
		loaded->overrideGameSetting(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION, JsonNode());
		if(historicalPerkPolicy)
		{
			auto perks = loaded->getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS);
			perks.Struct().erase("quickStudyPolicy");
			perks.setOverrideFlag(true);
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
		}
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
	const auto dwellings = map()->getObjects<CGDwelling>();
	const auto found = std::find_if(dwellings.begin(), dwellings.end(), [](const auto * candidate)
	{
		return candidate->ID == Obj::CREATURE_GENERATOR1;
	});
	ASSERT_NE(found, dwellings.end());
	auto * dwelling = *found;
	ASSERT_EQ(dwelling->ID, Obj::CREATURE_GENERATOR1);
	const CreatureID guard(CreatureID::decode("core:angel"));
	dwelling->creatures = {{17, {guard}}};
	dwelling->clearSlots();
	GameHandlerTestServer server(gameState());
	CGameHandler handler(server, gameState());
	const int raw = gameState()->getCreatureBaseGrowth(guard) * 3;
	const int scaled = (raw + 1) / 2;
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
	ASSERT_EQ(bank->getOwner(), PlayerColor::UNFLAGGABLE);
	const auto constructor = std::dynamic_pointer_cast<const CRewardableConstructor>(bank->getObjectHandler());
	ASSERT_NE(constructor, nullptr);
	for(int round = 0; round < 2; ++round)
	{
		bank->configuration.resetParameters.days = 1;
		bank->configuration.resetParameters.rewards = true;
		gameState()->day = round + 2;
		handler.randomizer->setSeed(1337 + round);
		GameRandomizer reference(*gameState());
		// A seed alone omits prior artifact allocations and independent streams.
		CMemorySerializer randomizerCopy(bytes(*handler.randomizer));
		randomizerCopy.iser & reference;
		ASSERT_EQ(bytes(reference), bytes(*handler.randomizer));
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

namespace
{
std::shared_ptr<CGameState> calibratedRoundTrip(const CGameState & source,
	ESerializationVersion version = ESerializationVersion::CURRENT)
{
	NativeBonusGraphAccess graphAccess;
	CMemorySerializer memory;
	memory.oser.version = memory.iser.version = version;
	memory.oser & source;
	auto restored = std::make_shared<CGameState>();
	restored->preInit(LIBRARY);
	memory.iser.loadingGamestate = true;
	memory.iser.cb = restored.get();
	memory.iser & *restored;
	return restored;
}

JsonNode fixedCalibration()
{
	JsonNode result;
	result["version"].Integer() = 1;
	result["enabled"].Bool() = true;
	for(const auto [input, output] : {std::pair{1000, 1000}, std::pair{5000, 2000}})
	{
		JsonNode point;
		point["original"].Integer() = input;
		point["fieldable"].Integer() = output;
		result["curve"].Vector().push_back(point);
	}
	return result;
}

class NewHorizonsNeutralCalibrationWorldTest : public NewHorizonsNeutralArmyWorldTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		NewHorizonsNeutralArmyWorldTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION, fixedCalibration());
	}
};
}

TEST_F(NewHorizonsNeutralCalibrationWorldTest, FreshNumbersRoundTripWithoutSecondScaling)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * monster = findFirst<CGCreature>();
	ASSERT_NE(monster, nullptr);
	EXPECT_EQ(monster->getStackCount(SlotID(0)), 11);
	EXPECT_EQ(monster->temppower, 11000);
	// The seven-slot resource formation shares one budget, not seven ceilings.
	const auto * pile = findFirst<CGResource>();
	ASSERT_NE(pile, nullptr);
	int total = 0;
	for(const auto & [slot, stack] : pile->Slots())
		total += stack->getCount();
	EXPECT_EQ(total, 8);
	EXPECT_EQ(findFirst<CGTownInstance>()->getStackCount(SlotID(0)), 4);
	auto restored = calibratedRoundTrip(*gameState());
	EXPECT_EQ(restored->getMap().getObjects<CGCreature>().front()->getStackCount(SlotID(0)), 11);
	EXPECT_EQ(restored->getSettings().getValue(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION), fixedCalibration());
	auto reread = calibratedRoundTrip(*restored);
	EXPECT_EQ(reread->getMap().getObjects<CGCreature>().front()->temppower, 11000);
}

TEST_F(NewHorizonsNeutralCalibrationWorldTest, ScriptedEncounterKeepsPercentageWithoutNewRng)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CMap isolated(gameState().get());
	isolated.overrideGameSetting(option, JsonNode(50));
	isolated.overrideGameSetting(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION, fixedCalibration());
	auto ordinary = initialArmy<CGCreature>(isolated, PlayerColor::NEUTRAL, 43);
	auto scripted = initialArmy<CGCreature>(isolated, PlayerColor::NEUTRAL, 43);
	scripted->message.appendRawString("Scripted encounter");
	const auto before = bytes(dynamic_cast<CRandomGenerator &>(*getRNG()));
	newHorizonsNeutralArmies::initializeWorld(isolated);
	EXPECT_EQ(ordinary->getStackCount(SlotID(0)), 11);
	EXPECT_EQ(scripted->getStackCount(SlotID(0)), 22);
	EXPECT_EQ(bytes(dynamic_cast<CRandomGenerator &>(*getRNG())), before);
}

TEST_F(NewHorizonsNeutralCalibrationWorldTest, UnsupportedWorldEnvelopesRejectBeforePrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto rejects = [](const auto & value)
	{
		CMemorySerializer memory;
		memory.oser.version = ESerializationVersion::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES;
		EXPECT_THROW(memory.oser & value, std::runtime_error);
		EXPECT_TRUE(memory.extractBuffer().empty());
	};
	rejects(*map());
	rejects(*gameState());
	LobbyStartGame start;
	start.initializedGameState = gameState();
	rejects(start);
}

TEST_F(NewHorizonsNeutralCalibrationWorldTest, BankConfigurationAndPhysicalStacksShareOneBudget)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CMap isolated(gameState().get());
	isolated.overrideGameSetting(option, JsonNode(50));
	isolated.overrideGameSetting(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION, fixedCalibration());
	auto bank = initialArmy<CRewardableObject>(isolated, PlayerColor::UNFLAGGABLE, 100);
	bank->configuration.info.emplace_back();
	auto & reward = bank->configuration.info.front().reward;
	for(int slot = 0; slot < 5; ++slot)
	{
		reward.guards.emplace_back(troop(), 100);
		if(slot > 0)
			bank->putStack(SlotID(slot), std::make_unique<CStackInstance>(gameState().get(), troop(), 100));
	}
	reward.creatures.emplace_back(troop(), 17);
	newHorizonsNeutralArmies::initializeWorld(isolated);
	for(int slot = 0; slot < 5; ++slot)
	{
		EXPECT_EQ(bank->getStackCount(SlotID(slot)), 3);
		EXPECT_EQ(reward.guards[slot].getCount(), 3);
	}
	EXPECT_EQ(reward.creatures.front().getCount(), 17);
}

TEST_F(NewHorizonsNeutralCalibrationWorldTest, RefillUsesCapturedCurveAndPreservesRandomStream)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto restored = calibratedRoundTrip(*gameState());
	GameHandlerTestServer server(restored);
	CGameHandler handler(server, restored);
	auto object = handler.createNewObject({20, 14, 0}, Obj::DRAGON_UTOPIA, MapObjectSubID(0));
	handler.newObject(object, PlayerColor(0));
	auto * bank = dynamic_cast<CRewardableObject *>(object.get());
	ASSERT_NE(bank, nullptr);
	const auto constructor = std::dynamic_pointer_cast<const CRewardableConstructor>(bank->getObjectHandler());
	ASSERT_NE(constructor, nullptr);
	bank->configuration.resetParameters.days = 1;
	bank->configuration.resetParameters.rewards = true;
	restored->day = 2;
	handler.randomizer->setSeed(1337);
	GameRandomizer reference(*restored);
	CMemorySerializer copy(bytes(*handler.randomizer));
	copy.iser & reference;
	auto expected = constructor->generateConfiguration(bank->cb, reference, bank->ID, bank->configuration.variables.preset);
	newHorizonsNeutralArmies::scaleGeneratedGuards(*bank, expected);
	bank->newTurn(handler, *handler.randomizer);
	EXPECT_EQ(bytes(*handler.randomizer), bytes(reference));
	ASSERT_EQ(bank->configuration.info.size(), expected.info.size());
	int slot = 0;
	for(size_t i = 0; i < expected.info.size(); ++i)
	{
		EXPECT_EQ(bytes(bank->configuration.info[i].reward), bytes(expected.info[i].reward));
		for(const auto & stack : expected.info[i].reward.guards)
			EXPECT_EQ(bank->getStackCount(SlotID(slot++)), stack.getCount());
	}
	EXPECT_GT(slot, 0);
}

TEST_F(NewHorizonsNeutralArmyWorldTest, LegacyDisabledWorldKeepsPercentageAndOrdinaryGrowthAfterResave)
{
	historicalPerkPolicy = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto restored = calibratedRoundTrip(*gameState(), ESerializationVersion::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES);
	auto reread = calibratedRoundTrip(*restored);
	EXPECT_FALSE(newHorizonsNeutralArmies::calibration::Rules(reread->getSettings().getValue(
		EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION)).enabled());
	GameHandlerTestServer server(reread);
	CGameHandler handler(server, reread);
	auto * monster = reread->getMap().getObjects<CGCreature>().front();
	EXPECT_EQ(monster->getStackCount(SlotID(0)), 22);
	reread->day = 8;
	monster->newTurn(handler, *handler.randomizer);
	EXPECT_EQ(monster->getStackCount(SlotID(0)), 24);
	EXPECT_EQ(monster->temppower, 24200);
}

TEST_F(NewHorizonsNeutralCalibrationWorldTest, QuestTargetKeepsAuthoredEncounterPercentage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CMap isolated(gameState().get());
	isolated.overrideGameSetting(option, JsonNode(50));
	isolated.overrideGameSetting(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION, fixedCalibration());
	auto target = initialArmy<CGCreature>(isolated, PlayerColor::NEUTRAL, 43);
	target->id = ObjectInstanceID(5);
	auto ordinary = initialArmy<CGCreature>(isolated, PlayerColor::NEUTRAL, 43);
	ordinary->id = ObjectInstanceID(6);
	auto seer = initialArmy<SeerHut>(isolated, PlayerColor::UNFLAGGABLE, 1);
	seer->addQuest().mission.destroyedObjects.push_back(target->id);
	newHorizonsNeutralArmies::initializeWorld(isolated);
	EXPECT_EQ(target->getStackCount(SlotID(0)), 22);
	EXPECT_EQ(ordinary->getStackCount(SlotID(0)), 11);
}

TEST_F(NewHorizonsNeutralCalibrationWorldTest, HundredPercentUsesCalibratedBaseline)
{
	percent = 100;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(findFirst<CGCreature>()->getStackCount(SlotID(0)), 21);
	EXPECT_EQ(findFirst<CGCreature>()->temppower, 21000);
	EXPECT_EQ(newHorizonsNeutralArmies::percentage(gameState()->getSettings()), 100);
}

TEST_F(NewHorizonsNeutralArmyWorldTest, HistoricalWireAbsenceResavesAndRefillsWithoutCalibration)
{
	historicalPerkPolicy = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	// A null override is omitted from the settings payload. This is actual wire
	// absence, distinct from an explicitly captured disabled calibration row.
	map()->overrideGameSetting(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION, JsonNode());
	auto restored = calibratedRoundTrip(*gameState(), ESerializationVersion::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES);
	EXPECT_FALSE(newHorizonsNeutralArmies::calibration::Rules(restored->getSettings().getValue(
		EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION)).enabled());
	auto reread = calibratedRoundTrip(*restored);
	EXPECT_EQ(reread->getMap().getObjects<CGCreature>().front()->getStackCount(SlotID(0)), 22);
	GameHandlerTestServer server(reread);
	CGameHandler handler(server, reread);
	auto object = handler.createNewObject({20, 14, 0}, Obj::DRAGON_UTOPIA, MapObjectSubID(0));
	handler.newObject(object, PlayerColor(0));
	auto * bank = dynamic_cast<CRewardableObject *>(object.get());
	ASSERT_NE(bank, nullptr);
	const auto constructor = std::dynamic_pointer_cast<const CRewardableConstructor>(bank->getObjectHandler());
	ASSERT_NE(constructor, nullptr);
	bank->configuration.resetParameters.days = 1;
	bank->configuration.resetParameters.rewards = true;
	reread->day = 2;
	handler.randomizer->setSeed(1337);
	GameRandomizer reference(*reread);
	CMemorySerializer copy(bytes(*handler.randomizer));
	copy.iser & reference;
	auto expected = constructor->generateConfiguration(bank->cb, reference, bank->ID, bank->configuration.variables.preset);
	for(auto & visit : expected.info)
		for(auto & guard : visit.reward.guards)
			guard.setCount((guard.getCount() + 1) / 2);
	bank->newTurn(handler, *handler.randomizer);
	ASSERT_EQ(bank->configuration.info.size(), expected.info.size());
	for(size_t i = 0; i < expected.info.size(); ++i)
		EXPECT_EQ(bytes(bank->configuration.info[i].reward), bytes(expected.info[i].reward));
	EXPECT_EQ(bytes(*handler.randomizer), bytes(reference));
}

TEST_F(NewHorizonsNeutralCalibrationWorldTest, ExplicitDefinitionCapturesOnceIndependentOfNeutralPercentage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	using namespace newHorizonsNeutralArmies::calibration;
	JsonNode definition;
	definition["version"].Integer() = 1;
	definition["enabled"].Bool() = true;
	for(const int week : {1, 1, 1, 2, 2, 3, 4})
		definition["unlockWeeks"].Vector().emplace_back(week);
	for(const auto [level, weeks, lines, gold] : {std::tuple{1, 1, 3, 4000}, std::tuple{10, 4, 6, 18000}})
	{
		JsonNode stage;
		stage["heroLevel"].Integer() = level;
		stage["weeks"].Integer() = weeks;
		stage["unlockedLines"].Integer() = lines;
		stage["troopGold"].Integer() = gold;
		definition["stages"].Vector().push_back(stage);
	}
	GameSettings reference;
	reference.addOverride(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
		JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
	reference.addOverride(EGameSettings::CREATURES_NEW_HORIZONS_CATEGORIES,
		JsonNode(JsonPath::builtin("config/newHorizonsCreatureCategories")));
	reference.addOverride(EGameSettings::CREATURES_RECRUITMENT_GROWTH_PERCENT, JsonNode(50));
	reference.addOverride(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION, definition);
	reference.addOverride(option, JsonNode(1));
	const auto captured = capture(reference);
	EXPECT_TRUE(Rules(captured).enabled());
	EXPECT_TRUE(captured.Struct().contains("curve"));
	EXPECT_FALSE(captured.Struct().contains("stages"));
	reference.addOverride(option, JsonNode(100));
	EXPECT_EQ(capture(reference), captured);
	// Once captured, later installed supply defaults cannot recalculate this row.
	reference.addOverride(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION, captured);
	reference.addOverride(EGameSettings::CREATURES_RECRUITMENT_GROWTH_PERCENT, JsonNode(100));
	EXPECT_EQ(capture(reference), captured);
}

namespace
{
struct NeutralSaveFiles
{
	boost::filesystem::path directory = boost::filesystem::temp_directory_path()
		/ boost::filesystem::unique_path("nh-neutral-save-%%%%%%%%-%%%%%%%%");
	NeutralSaveFiles() { boost::filesystem::create_directory(directory); }
	~NeutralSaveFiles() { boost::filesystem::remove_all(directory); }
};

std::shared_ptr<CGameState> emptyNeutralWorld()
{
	auto world = std::make_shared<CGameState>();
	world->preInit(LIBRARY);
	return world;
}

void writeNeutralSave(const boost::filesystem::path & path, const CGameState & world, const CGameHandler & handler)
{
	NativeBonusGraphAccess graphAccess;
	CSaveFile file;
	world.saveGame(file);
	file.save(handler);
	file.write(path);
}

void readNeutralSave(const boost::filesystem::path & path, CGameState & world, CGameHandler & handler)
{
	NativeBonusGraphAccess graphAccess;
	CLoadFile file(path, &world);
	EXPECT_EQ(file.formatVersion(), ESerializationVersion::CURRENT);
	world.loadGame(file);
	file.load(handler);
	EXPECT_TRUE(file.atEnd());
}

void expectNeutralRefill(const std::shared_ptr<CGameState> & world, CGameHandler & handler, bool calibrated)
{
	auto object = handler.createNewObject({20, 14, 0}, Obj::DRAGON_UTOPIA, MapObjectSubID(0));
	handler.newObject(object, PlayerColor(0));
	auto * bank = dynamic_cast<CRewardableObject *>(object.get());
	ASSERT_NE(bank, nullptr);
	const auto constructor = std::dynamic_pointer_cast<const CRewardableConstructor>(bank->getObjectHandler());
	ASSERT_NE(constructor, nullptr);
	bank->configuration.resetParameters.days = 1;
	bank->configuration.resetParameters.rewards = true;
	world->day = 2;
	handler.randomizer->setSeed(1337);
	GameRandomizer reference(*world);
	CMemorySerializer copy(bytes(*handler.randomizer));
	copy.iser & reference;
	auto expected = constructor->generateConfiguration(bank->cb, reference, bank->ID, bank->configuration.variables.preset);
	if(calibrated)
		newHorizonsNeutralArmies::scaleGeneratedGuards(*bank, expected);
	else
		for(auto & visit : expected.info)
			for(auto & guard : visit.reward.guards)
				guard.setCount((guard.getCount() + 1) / 2);
	bank->newTurn(handler, *handler.randomizer);
	ASSERT_EQ(bank->configuration.info.size(), expected.info.size());
	int slot = 0;
	for(size_t i = 0; i < expected.info.size(); ++i)
	{
		EXPECT_EQ(bytes(bank->configuration.info[i].reward), bytes(expected.info[i].reward));
		for(const auto & guard : expected.info[i].reward.guards)
			EXPECT_EQ(bank->getStackCount(SlotID(slot++)), guard.getCount());
	}
	EXPECT_GT(slot, 0);
	EXPECT_EQ(bytes(*handler.randomizer), bytes(reference));
}
}

TEST_F(NewHorizonsNeutralCalibrationWorldTest, RealCurrentFileResavesCapturedCountsAndRefillsWithoutRngDrift)
{
	ASSERT_GE(ESerializationVersion::CURRENT, ESerializationVersion::NEW_HORIZONS_LEADERSHIP_NEUTRAL_CALIBRATION);
	ASSERT_NO_FATAL_FAILURE(prepare());
	NeutralSaveFiles files;
	GameHandlerTestServer sourceServer(gameState());
	CGameHandler sourceHandler(sourceServer, gameState());
	sourceHandler.randomizer->setSeed(9182);
	const auto randomBefore = bytes(*sourceHandler.randomizer);
	writeNeutralSave(files.directory / "first.vsgm1", *gameState(), sourceHandler);
	auto restored = emptyNeutralWorld();
	GameHandlerTestServer restoredServer(restored);
	CGameHandler restoredHandler(restoredServer, restored);
	readNeutralSave(files.directory / "first.vsgm1", *restored, restoredHandler);
	EXPECT_EQ(bytes(*restoredHandler.randomizer), randomBefore);
	EXPECT_EQ(restored->getSettings().getValue(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION), fixedCalibration());
	EXPECT_EQ(restored->getMap().getObjects<CGCreature>().front()->getStackCount(SlotID(0)), 11);
	EXPECT_EQ(restored->getMap().getObjects<CGCreature>().front()->temppower, 11000);
	writeNeutralSave(files.directory / "second.vsgm1", *restored, restoredHandler);
	auto reread = emptyNeutralWorld();
	GameHandlerTestServer rereadServer(reread);
	CGameHandler rereadHandler(rereadServer, reread);
	readNeutralSave(files.directory / "second.vsgm1", *reread, rereadHandler);
	EXPECT_EQ(bytes(*rereadHandler.randomizer), randomBefore);
	EXPECT_EQ(reread->getSettings().getValue(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION), fixedCalibration());
	EXPECT_EQ(reread->getMap().getObjects<CGCreature>().front()->getStackCount(SlotID(0)), 11);
	EXPECT_EQ(reread->getMap().getObjects<CGCreature>().front()->temppower, 11000);
	ASSERT_NO_FATAL_FAILURE(expectNeutralRefill(reread, rereadHandler, true));
}

TEST_F(NewHorizonsNeutralCalibrationWorldTest, CurrentReplaySnapshotKeepsVersionCaptureAndCountsThroughResave)
{
	ASSERT_GE(ESerializationVersion::CURRENT, ESerializationVersion::NEW_HORIZONS_LEADERSHIP_NEUTRAL_CALIBRATION);
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto randomBefore = bytes(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto snapshot = gameState()->saveReplaySnapshot();
	EXPECT_EQ(snapshot.version, ESerializationVersion::CURRENT);
	auto restored = emptyNeutralWorld();
	restored->loadReplaySnapshot(snapshot);
	const auto recaptured = restored->saveReplaySnapshot();
	EXPECT_EQ(recaptured.version, snapshot.version);
	restored->loadReplaySnapshot(recaptured);
	EXPECT_EQ(restored->saveReplaySnapshot(), recaptured);
	EXPECT_EQ(restored->getSettings().getValue(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION), fixedCalibration());
	EXPECT_EQ(restored->getMap().getObjects<CGCreature>().front()->getStackCount(SlotID(0)), 11);
	EXPECT_EQ(restored->getMap().getObjects<CGCreature>().front()->temppower, 11000);
	EXPECT_EQ(bytes(dynamic_cast<CRandomGenerator &>(*getRNG())), randomBefore);
	GameHandlerTestServer server(restored);
	CGameHandler handler(server, restored);
	ASSERT_NO_FATAL_FAILURE(expectNeutralRefill(restored, handler, true));
}

TEST_F(NewHorizonsNeutralArmyWorldTest, HistoricalReplayAbsenceStaysDisabledUnderEnabledDefaultsThroughCurrentResave)
{
	ASSERT_GE(ESerializationVersion::CURRENT, ESerializationVersion::NEW_HORIZONS_LEADERSHIP_NEUTRAL_CALIBRATION);
	historicalPerkPolicy = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	// The installed new-world definition really is enabled; historical absence
	// must not inherit it. The old payload below is written at its actual epoch.
	CMap installed(gameState().get());
	ASSERT_TRUE(installed.getSettings().getValue(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION)["enabled"].Bool());
	map()->overrideGameSetting(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION, JsonNode());
	const auto randomBefore = bytes(dynamic_cast<CRandomGenerator &>(*getRNG()));
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES;
	{
		NativeBonusGraphAccess graphAccess;
		old.oser & *gameState();
	}
	const ReplayPayload historical{old.extractBuffer(), old.oser.version};
	auto restored = emptyNeutralWorld();
	restored->loadReplaySnapshot(historical);
	EXPECT_FALSE(newHorizonsNeutralArmies::calibration::Rules(restored->getSettings().getValue(
		EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION)).enabled());
	const auto current = restored->saveReplaySnapshot();
	EXPECT_EQ(current.version, ESerializationVersion::CURRENT);
	EXPECT_NE(current.version, historical.version);
	restored->loadReplaySnapshot(current);
	EXPECT_EQ(restored->saveReplaySnapshot(), current);
	EXPECT_FALSE(newHorizonsNeutralArmies::calibration::Rules(restored->getSettings().getValue(
		EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION)).enabled());
	EXPECT_EQ(restored->getMap().getObjects<CGCreature>().front()->getStackCount(SlotID(0)), 22);
	EXPECT_EQ(restored->getMap().getObjects<CGCreature>().front()->temppower, 22000);
	EXPECT_EQ(bytes(dynamic_cast<CRandomGenerator &>(*getRNG())), randomBefore);
	GameHandlerTestServer server(restored);
	CGameHandler handler(server, restored);
	ASSERT_NO_FATAL_FAILURE(expectNeutralRefill(restored, handler, false));
}

TEST_F(NewHorizonsNeutralCalibrationWorldTest, NeverFleeAloneKeepsCalibrationWhileScriptedRewardsRemainProtected)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CMap isolated(gameState().get());
	isolated.overrideGameSetting(option, JsonNode(50));
	isolated.overrideGameSetting(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION, fixedCalibration());
	auto ordinary = initialArmy<CGCreature>(isolated, PlayerColor::NEUTRAL, 43);
	auto resolute = initialArmy<CGCreature>(isolated, PlayerColor::NEUTRAL, 43);
	resolute->neverFlees = true;
	auto scripted = initialArmy<CGCreature>(isolated, PlayerColor::NEUTRAL, 43);
	scripted->neverFlees = true;
	scripted->message.appendRawString("Scripted encounter");
	const auto randomBefore = bytes(dynamic_cast<CRandomGenerator &>(*getRNG()));
	newHorizonsNeutralArmies::initializeWorld(isolated);
	EXPECT_EQ(ordinary->getStackCount(SlotID(0)), 11);
	EXPECT_EQ(resolute->getStackCount(SlotID(0)), 11);
	EXPECT_EQ(scripted->getStackCount(SlotID(0)), 22);
	EXPECT_TRUE(resolute->neverFlees);
	EXPECT_TRUE(scripted->neverFlees);
	EXPECT_EQ(bytes(dynamic_cast<CRandomGenerator &>(*getRNG())), randomBefore);
}

TEST_F(NewHorizonsNeutralCalibrationWorldTest, AllForOneFireElementalFlagsDoNotBypassCapturedSizing)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CMap isolated(gameState().get());
	isolated.overrideGameSetting(option, JsonNode(50));
	isolated.overrideGameSetting(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION, fixedCalibration());
	// Original ROE guard 725 is 60 Fire Elementals, Savage/NeverFlees, with
	// no message, reward or no-growth tag. Use those inputs with this fixture's
	// synthetic captured curve, rather than claiming a live-map balance result.
	auto guard = initialArmy<CGCreature>(isolated, PlayerColor::NEUTRAL, 60);
	guard->getStackPtr(SlotID(0))->setType(CreatureID(CreatureID::decode("core:fireElemental")));
	guard->initialCharacter = CGCreature::Character::SAVAGE;
	guard->neverFlees = true;
	newHorizonsNeutralArmies::initializeWorld(isolated);
	EXPECT_GT(guard->getStackCount(SlotID(0)), 0);
	EXPECT_LT(guard->getStackCount(SlotID(0)), 30);
	EXPECT_EQ(guard->temppower, static_cast<int64_t>(guard->getStackCount(SlotID(0))) * 1000);
	EXPECT_TRUE(guard->neverFlees);
	EXPECT_EQ(guard->initialCharacter, CGCreature::Character::SAVAGE);
}
