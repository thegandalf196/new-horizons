/* GarrisonPreparationTest.cpp, part of VCMI engine; GPL v2.0 or later. */
#include "StdInc.h"
#include "nullkiller2/NullkillerTest.h"
#include "mock/GameHandlerTestServer.h"
#include "AI/Nullkiller2/Analyzers/ArmyManager.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "AI/Nullkiller2/AIUtility.h"
#include "AI/Nullkiller2/Goals/ExchangeSwapTownHeroes.h"
#include "lib/gameState/UpgradeInfo.h"
#include "AI/Nullkiller2/Behaviors/TownGarrisonAdmission.h"
#include "AI/Nullkiller2/Analyzers/DangerHitMapAnalyzer.h"
#include "AI/Nullkiller2/Helpers/ArmyExchangeExecution.h"
#include "AI/Nullkiller2/Helpers/ArmyFormation.h"
#include "lib/CStack.h"
#include "lib/GameLibrary.h"
#include "lib/callback/CCallback.h"
#include "lib/callback/IClient.h"
#include "lib/entities/hero/CHeroHandler.h"
#include "lib/entities/hero/NewHorizonsLeadership.h"
#include "lib/mapObjects/CGTownInstance.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapping/TerrainTile.h"
#include "lib/mapObjects/army/CStackInstance.h"
#include "lib/networkPacks/PacksForClient.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/serializer/CMemorySerializer.h"
#include "server/CGameHandler.h"
#include <map>
#include <stdexcept>
#include <set>
#include <tuple>

namespace
{
constexpr PlayerColor OWNER(0);
using Army = std::map<SlotID, std::pair<CreatureID, TQuantity>>;
template<class T> std::vector<std::byte> serialized(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}

class GarrisonServer final : public GameHandlerTestServer
{
public:
	std::vector<bool> accepted;
	NK2AI::AIGateway * gateway = nullptr;
	explicit GarrisonServer(std::shared_ptr<CGameState> state) : GameHandlerTestServer(state, OWNER) {}
	void applyPack(CPackForClient & pack) override
	{
		GameHandlerTestServer::applyPack(pack);
		if(!gateway)
			return;
		if(const auto * blocked = dynamic_cast<const PlayerBlocked *>(&pack); blocked && blocked->player == OWNER)
			gateway->playerBlocked(blocked->reason, blocked->startOrEnd == PlayerBlocked::BLOCKADE_STARTED);
		if(const auto * visit = dynamic_cast<const HeroVisit *>(&pack); visit && visit->player == OWNER)
			gateway->heroVisit(gateway->cc->getHero(visit->heroId),
				gateway->cc->getObj(visit->objId, false), visit->starting);
		if(const auto * move = dynamic_cast<const TryMoveHero *>(&pack))
			gateway->heroMoved(*move, true);
	}
	void sendPack(CPackForClient & pack, GameConnectionID) override
	{
		if(auto * reply = dynamic_cast<PackageApplied *>(&pack))
		{
			accepted.push_back(reply->result);
			if(gateway)
				gateway->requestRealized(reply);
		}
	}
};

class GarrisonLoopback final : public IClient
{
	CGameHandler & handler;
public:
	int arrangements = 0;
	int swaps = 0;
	int moves = 0;
	int disbands = 0;
	NK2AI::AIGateway * gateway = nullptr;
	std::vector<bool> waits;
	explicit GarrisonLoopback(CGameHandler & handler) : handler(handler) {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor,
		const BattleID &, const BattleStateInfoForRetreat &) override { return std::nullopt; }
	int sendRequest(const CPackForServer & request, PlayerColor player, bool wait) override
	{
		auto incoming = CMemorySerializer::deepCopy(request);
		incoming->player = player;
		incoming->requestID = arrangements + swaps + moves + disbands + 1;
		if(dynamic_cast<const ArrangeStacks *>(incoming.get()))
			++arrangements;
		else if(dynamic_cast<const GarrisonHeroSwap *>(incoming.get()))
			++swaps;
		else if(dynamic_cast<const MoveHero *>(incoming.get()))
			++moves;
		else if(dynamic_cast<const DisbandCreature *>(incoming.get()))
		{
			++disbands;
			throw std::runtime_error("Garrison preparation must conserve every creature");
		}
		else
			throw std::runtime_error("Unexpected garrison preparation command");
		waits.push_back(wait);
		if(gateway)
			gateway->requestSent(incoming.get(), incoming->requestID);
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, *incoming);
		return incoming->requestID;
	}
};

class GarrisonPreparationTest : public NullkillerTest
{
protected:
	CGHeroInstance * hero = nullptr;
	CGTownInstance * town = nullptr;
	std::unique_ptr<GarrisonServer> server;
	std::unique_ptr<CGameHandler> handler;
	std::unique_ptr<GarrisonLoopback> client;
	std::shared_ptr<CCallback> callback;
	std::unique_ptr<NK2AI::AIGateway> gateway;
	bool initialWaitTillRealize = false;

	Services * gameServices() override { return LIBRARY; }
	void configurePlayer(PlayerSettings & settings) const override
	{
		settings.bonus = PlayerStartingBonus::GOLD;
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		for(const auto & [setting, path] : std::vector<std::pair<EGameSettings, std::string>>{
			{EGameSettings::HEROES_NEW_HORIZONS, "config/newHorizonsHeroes"},
			{EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, "config/newHorizonsCapabilities"},
			{EGameSettings::HEROES_NEW_HORIZONS_PERKS, "config/newHorizonsPerks"}})
			loaded->overrideGameSetting(setting, JsonNode(JsonPath::builtin(path)));
	}
	void prepare(const std::string & heroName, int level,
		std::vector<std::pair<CreatureID, uint16_t>> heroArmy,
		std::vector<std::pair<CreatureID, uint16_t>> townArmy, bool secondHero = false, FactionID townFaction = FactionID::TOWER)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(OWNER).playerActive(PlayerColor(1))
			.town({12, 12, 0}, townFaction, OWNER).townGarrison(std::move(townArmy))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(heroName)), OWNER)
			.heroExperience(LIBRARY->heroh->reqExp(level)).heroGarrison(std::move(heroArmy));
		if(secondHero)
			builder.hero({8, 5, 0}, HeroTypeID(HeroTypeID::decode("core:solmyr")), OWNER)
				.heroExperience(0).heroGarrison({{CreatureID(CreatureID::decode("core:mage")), 1}});
		builder.town({26, 26, 0}, FactionID::CASTLE, PlayerColor(1));
		ASSERT_NO_FATAL_FAILURE(startWithMap(std::move(builder)));
		hero = findHeroAt({5, 5, 0});
		for(auto * candidate : findAll<CGTownInstance>())
			if(candidate->getOwner() == OWNER)
				town = candidate;
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(town, nullptr);
		ASSERT_EQ(hero->level, level);
		ASSERT_EQ(findAll<CGTownInstance>().size(), 2);
		ChangeObjPos move;
		move.objid = hero->id;
		move.nPos = town->visitablePos();
		move.initiator = OWNER;
		gameState()->apply(move);
		// ChangeObjPos already registered the visible hero on the entrance tile.
		HeroVisitCastle visiting;
		visiting.tid = town->id;
		visiting.hid = hero->id;
		visiting.startVisit = true;
		gameState()->apply(visiting);
		ASSERT_EQ(town->getVisitingHero(), hero);
		ASSERT_EQ(town->getGarrisonHero(), nullptr);
		server = std::make_unique<GarrisonServer>(gameState());
		handler = std::make_unique<CGameHandler>(*server, gameState());
		gameState()->actingPlayers.insert(OWNER);
		client = std::make_unique<GarrisonLoopback>(*handler);
		callback = makeCallback(OWNER, client.get());
		gateway = makeGateway(callback);
		initialWaitTillRealize = callback->waitTillRealize;
		ASSERT_TRUE(initialWaitTillRealize);
	}
	static Army snapshot(const CArmedInstance * army)
	{
		Army result;
		for(const auto & [slot, stack] : army->Slots())
			result[slot] = {stack->getCreatureID(), stack->getCount()};
		return result;
	}
	std::map<CreatureID, int64_t> totals() const
	{
		std::map<CreatureID, int64_t> result;
		for(const CArmedInstance * army : {static_cast<const CArmedInstance *>(hero),
			static_cast<const CArmedInstance *>(town)})
			for(const auto & [slot, stack] : army->Slots())
				result[stack->getCreatureID()] += stack->getCount();
		return result;
	}
	auto rngState()
	{
		return std::tuple(serialized(*handler->randomizer),
			serialized(dynamic_cast<CRandomGenerator &>(*getRNG())),
			serialized(CRandomGenerator::getDefault()));
	}
	void checkAccepted() const
	{
		EXPECT_EQ(server->accepted.size(), client->waits.size());
		for(bool accepted : server->accepted)
			EXPECT_TRUE(accepted);
		EXPECT_EQ(callback->waitTillRealize, initialWaitTillRealize);
		RecordProperty("ArrangeCommands", client->arrangements);
		RecordProperty("GarrisonSwapCommands", client->swaps);
		RecordProperty("AcceptedCommands", static_cast<int>(server->accepted.size()));
	}
	void checkCapacities() const
	{
		for(const auto & [slot, stack] : hero->Slots())
		{
			const auto capacity = hero->getLeadershipSlotCapacity(stack->getCreatureID());
			ASSERT_TRUE(capacity);
			EXPECT_LE(stack->getCount(), capacity->maximum);
		}
	}
	void checkAdmission(bool expected)
	{
		const auto before = gameState()->saveToMemory();
		const auto rng = rngState();
		NK2AI::HitMapInfo threat;
		threat.turn = 0;
		threat.danger = 1;
		EXPECT_EQ(NK2AI::canPrepareCurrentTownGarrison(*town,
			*gateway->nullkiller->armyManager, *callback), expected);
		EXPECT_EQ(NK2AI::currentVisitorGarrisonDefence(*town, threat,
			*gateway->nullkiller->armyManager, *callback).has_value(), expected);
		EXPECT_EQ(gameState()->saveToMemory(), before);
		EXPECT_EQ(rngState(), rng);
		EXPECT_TRUE(client->waits.empty());
	}
	void arrange()
	{
		NK2AI::armyFormation::ArmyExchangeProjection plan;
		const auto before = gameState()->saveToMemory();
		const auto army = gateway->nullkiller->armyManager->getBestArmy(
			hero, hero, town, callback->getTile(town->visitablePos())->getTerrainID(), nullptr, &plan);
		EXPECT_FALSE(army.empty());
		EXPECT_EQ(gameState()->saveToMemory(), before);
		NK2AI::armyFormation::executeArmyExchange(callback, hero, town, plan);
	}
	void TearDown() override
	{
		gateway.reset();
		callback.reset();
		client.reset();
		handler.reset();
		server.reset();
		NullkillerTest::TearDown();
	}
};
}

TEST_F(GarrisonPreparationTest, RissaConsolidatesDuplicateCabirsBeforeAdmittingTownCentaurs)
{
	const CreatureID naga(CreatureID::decode("core:naga"));
	const CreatureID centaur(CreatureID::decode("core:centaur"));
	const CreatureID cabir(CreatureID::decode("core:masterGremlin"));
	ASSERT_NO_FATAL_FAILURE(prepare("core:rissa", 7,
		{{naga, 2}, {centaur, 19}, {cabir, 16}, {centaur, 19}, {cabir, 1}, {cabir, 1}, {naga, 2}},
		{{centaur, 33}}));
	const auto centaurCap = hero->getLeadershipSlotCapacity(centaur);
	const auto nagaCap = hero->getLeadershipSlotCapacity(naga);
	const auto cabirCap = hero->getLeadershipSlotCapacity(cabir);
	ASSERT_TRUE(centaurCap && nagaCap && cabirCap);
	ASSERT_EQ(centaurCap->leadership, 1145);
	ASSERT_EQ(centaurCap->maximum, 19);
	ASSERT_EQ(nagaCap->maximum, 2);
	ASSERT_GE(cabirCap->maximum, 18);
	ASSERT_EQ(cabirCap->maximum, 22);
	ASSERT_EQ(centaurCap->requirement, 60);
	ASSERT_EQ(nagaCap->requirement, 450);
	ASSERT_EQ(cabirCap->requirement, 50);
	ASSERT_EQ(hero->stacksCount(), 7);
	checkAdmission(true);
	const auto initialTotals = totals();
	const auto rng = rngState();
	const auto movement = hero->movementPointsRemaining();
	ASSERT_NO_FATAL_FAILURE(arrange());
	EXPECT_EQ(totals(), initialTotals);
	EXPECT_EQ(town->stacksCount(), 0);
	EXPECT_EQ(hero->stacksCount(), 7);
	std::map<CreatureID, std::multiset<int64_t>> counts;
	for(const auto & [slot, stack] : hero->Slots())
		counts[stack->getCreatureID()].insert(stack->getCount());
	EXPECT_EQ(counts[naga], (std::multiset<int64_t>{2, 2}));
	EXPECT_EQ(counts[cabir], (std::multiset<int64_t>{18}));
	EXPECT_EQ(counts[centaur], (std::multiset<int64_t>{14, 19, 19, 19}));
	EXPECT_NO_THROW(gateway->swapGarrisonHero(town));
	EXPECT_EQ(town->getGarrisonHero(), hero);
	EXPECT_EQ(town->getVisitingHero(), nullptr);
	EXPECT_EQ(client->swaps, 1);
	EXPECT_GT(client->arrangements, 0);
	EXPECT_EQ(totals(), initialTotals);
	EXPECT_EQ(hero->movementPointsRemaining(), movement);
	EXPECT_EQ(rngState(), rng);
	checkAccepted();
	checkCapacities();
}

TEST_F(GarrisonPreparationTest, FullCapacityStacksLeaveUncommandableSurplusUntouched)
{
	const CreatureID mage(CreatureID::decode("core:mage"));
	ASSERT_NO_FATAL_FAILURE(prepare("core:solmyr", 1,
		std::vector<std::pair<CreatureID, uint16_t>>(7, {mage, 2}), {{mage, 1}}));
	const auto cap = hero->getLeadershipSlotCapacity(mage);
	ASSERT_TRUE(cap);
	ASSERT_EQ(cap->maximum, 2);
	ASSERT_TRUE(hero->canBeMergedWith(*town));
	checkAdmission(false);
	const auto initial = totals();
	const auto rng = rngState();
	const auto heroBefore = snapshot(hero);
	const auto townBefore = snapshot(town);
	ASSERT_NO_FATAL_FAILURE(arrange());
	EXPECT_EQ(snapshot(hero), heroBefore);
	EXPECT_EQ(snapshot(town), townBefore);
	EXPECT_FALSE(NK2AI::armyFormation::canSwapGarrisonHero(town));
	EXPECT_ANY_THROW(gateway->swapGarrisonHero(town));
	EXPECT_EQ(client->arrangements, 0);
	EXPECT_EQ(client->swaps, 0);
	EXPECT_EQ(totals(), initial);
	EXPECT_EQ(town->getVisitingHero(), hero);
	EXPECT_EQ(town->getGarrisonHero(), nullptr);
	EXPECT_EQ(rngState(), rng);
	checkAccepted();
	checkCapacities();
}

TEST_F(GarrisonPreparationTest, MageSurplusIsSplitBeforeValidatedGarrisonSwap)
{
	const CreatureID mage(CreatureID::decode("core:mage"));
	ASSERT_NO_FATAL_FAILURE(prepare("core:solmyr", 1, {{mage, 2}}, {{mage, 1}}));
	const auto cap = hero->getLeadershipSlotCapacity(mage);
	ASSERT_TRUE(cap);
	ASSERT_EQ(cap->maximum, 2);
	ASSERT_FALSE(NK2AI::armyFormation::canSwapGarrisonHero(town));
	checkAdmission(true);
	const auto initial = totals();
	const auto rng = rngState();
	ASSERT_NO_FATAL_FAILURE(arrange());
	EXPECT_EQ(town->stacksCount(), 0);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 2);
	EXPECT_EQ(hero->getStackCount(SlotID(1)), 1);
	EXPECT_TRUE(NK2AI::armyFormation::canSwapGarrisonHero(town));
	EXPECT_NO_THROW(gateway->swapGarrisonHero(town));
	EXPECT_EQ(client->arrangements, 1);
	EXPECT_EQ(client->swaps, 1);
	EXPECT_EQ(town->getGarrisonHero(), hero);
	EXPECT_EQ(totals(), initial);
	EXPECT_EQ(rngState(), rng);
	checkAccepted();
	checkCapacities();
}

TEST_F(GarrisonPreparationTest, ReceiverConsolidationCanLeaveLegalResidualTownArmy)
{
	const CreatureID naga(CreatureID::decode("core:naga"));
	const CreatureID centaur(CreatureID::decode("core:centaur"));
	ASSERT_NO_FATAL_FAILURE(prepare("core:rissa", 7,
		{{naga, 2}, {naga, 1}, {naga, 1}, {naga, 1}, {naga, 1}, {naga, 1}, {naga, 1}},
		{{centaur, 1}}));
	const auto cap = hero->getLeadershipSlotCapacity(naga);
	ASSERT_TRUE(cap);
	ASSERT_EQ(cap->leadership, 1145);
	ASSERT_EQ(cap->maximum, 2);
	ASSERT_FALSE(NK2AI::armyFormation::canSwapGarrisonHero(town));
	const auto initial = totals();
	const auto rng = rngState();
	const auto before = gameState()->saveToMemory();
	// A legitimate preferred receiver army can leave another creature in town.
	// This isolates the projector contract; it does not assert manager preferences.
	const std::vector<NK2AI::armyFormation::DesiredArmyStack> desired = {
		{naga, 2}, {naga, 2}, {naga, 2}, {naga, 2}};
	const auto plan = NK2AI::armyFormation::projectArmyExchange(hero, town, hero, nullptr, desired);
	EXPECT_EQ(gameState()->saveToMemory(), before);
	NK2AI::armyFormation::executeArmyExchange(callback, hero, town, plan);
	EXPECT_EQ(town->getStackCount(SlotID(0)), 1);
	EXPECT_EQ(hero->stacksCount(), 4);
	EXPECT_TRUE(NK2AI::armyFormation::canSwapGarrisonHero(town));
	EXPECT_NO_THROW(gateway->swapGarrisonHero(town));
	EXPECT_EQ(client->arrangements, 3);
	EXPECT_EQ(client->swaps, 1);
	EXPECT_EQ(town->getGarrisonHero(), hero);
	EXPECT_EQ(town->stacksCount(), 0);
	EXPECT_EQ(hero->stacksCount(), 5);
	EXPECT_EQ(totals(), initial);
	EXPECT_EQ(rngState(), rng);
	checkAccepted();
	checkCapacities();
}

TEST_F(GarrisonPreparationTest, GarrisonExtractionPreservesPreparedMageArmy)
{
	const CreatureID mage(CreatureID::decode("core:mage"));
	ASSERT_NO_FATAL_FAILURE(prepare("core:solmyr", 1, {{mage, 2}}, {{mage, 1}}));
	ASSERT_NO_FATAL_FAILURE(arrange());
	ASSERT_NO_THROW(gateway->swapGarrisonHero(town));
	ASSERT_EQ(town->getGarrisonHero(), hero);
	ASSERT_EQ(town->getVisitingHero(), nullptr);
	const auto army = snapshot(hero);
	const auto initial = totals();
	const auto rng = rngState();
	const auto arrangements = client->arrangements;
	ASSERT_TRUE(NK2AI::armyFormation::canSwapGarrisonHero(town));
	EXPECT_NO_THROW(gateway->swapGarrisonHero(town));
	EXPECT_EQ(town->getVisitingHero(), hero);
	EXPECT_EQ(town->getGarrisonHero(), nullptr);
	EXPECT_EQ(snapshot(hero), army);
	EXPECT_EQ(totals(), initial);
	EXPECT_EQ(client->arrangements, arrangements);
	EXPECT_EQ(client->swaps, 2);
	EXPECT_EQ(rngState(), rng);
	checkAccepted();
	checkCapacities();
}

TEST_F(GarrisonPreparationTest, TwoTownHeroesSwapRolesWithoutMergingDifferentCapacityArmies)
{
	const CreatureID mage(CreatureID::decode("core:mage"));
	ASSERT_NO_FATAL_FAILURE(prepare("core:rissa", 7, {{mage, 3}}, {}, true));
	auto * other = findHeroAt({8, 5, 0});
	ASSERT_NE(other, nullptr);
	ASSERT_NE(other, hero);
	const auto firstCap = hero->getLeadershipSlotCapacity(mage);
	const auto secondCap = other->getLeadershipSlotCapacity(mage);
	ASSERT_TRUE(firstCap && secondCap);
	ASSERT_EQ(firstCap->maximum, 3);
	ASSERT_EQ(secondCap->maximum, 2);
	ChangeObjPos relocate;
	relocate.objid = other->id;
	relocate.nPos = town->visitablePos();
	relocate.initiator = OWNER;
	gameState()->apply(relocate);
	ASSERT_EQ(hero->visitablePos(), town->visitablePos());
	ASSERT_EQ(other->visitablePos(), town->visitablePos());
	SetHeroesInTown roles;
	roles.tid = town->id;
	roles.visiting = ObjectInstanceID::NONE;
	roles.garrison = other->id;
	gameState()->apply(roles);
	HeroVisitCastle visiting;
	visiting.tid = town->id;
	visiting.hid = hero->id;
	visiting.startVisit = true;
	gameState()->apply(visiting);
	ASSERT_EQ(hero->getVisitedTown(), town);
	ASSERT_EQ(town->getVisitingHero(), hero);
	ASSERT_EQ(town->getGarrisonHero(), other);
	ASSERT_EQ(callback->getVisitableObjs(town->visitablePos()),
		(std::vector<const CGObjectInstance *>{town, hero}));
	ASSERT_EQ(map()->getTile(town->visitablePos()).visitableObjects,
		(std::vector<ObjectInstanceID>{town->id, hero->id}));
	const auto firstArmy = snapshot(hero);
	const auto secondArmy = snapshot(other);
	const auto bareTown = snapshot(town);
	const auto rng = rngState();
	ASSERT_TRUE(NK2AI::armyFormation::canSwapGarrisonHero(town));
	EXPECT_NO_THROW(gateway->swapGarrisonHero(town));
	EXPECT_EQ(town->getGarrisonHero(), hero);
	EXPECT_EQ(town->getVisitingHero(), other);
	EXPECT_EQ(map()->getTile(town->visitablePos()).visitableObjects,
		(std::vector<ObjectInstanceID>{town->id, other->id}));
	EXPECT_EQ(hero->visitablePos(), town->visitablePos());
	EXPECT_EQ(other->visitablePos(), town->visitablePos());
	EXPECT_EQ(snapshot(hero), firstArmy);
	EXPECT_EQ(snapshot(other), secondArmy);
	EXPECT_EQ(snapshot(town), bareTown);
	EXPECT_EQ(client->arrangements, 0);
	EXPECT_EQ(client->swaps, 1);
	EXPECT_EQ(rngState(), rng);
	checkAccepted();
}

TEST_F(GarrisonPreparationTest, InvalidReceiverConsolidationMetadataSubmitsNoCommand)
{
	const CreatureID mage(CreatureID::decode("core:mage"));
	ASSERT_NO_FATAL_FAILURE(prepare("core:solmyr", 1, {{mage, 2}}, {{mage, 1}}));
	const std::vector<NK2AI::armyFormation::DesiredArmyStack> desired = {{mage, 2}, {mage, 1}};
	const auto actual = NK2AI::armyFormation::projectArmyExchange(hero, town, hero, nullptr, desired);
	ASSERT_FALSE(actual.transfers.empty());
	ASSERT_EQ(actual.transfers.front().sourceSide, NK2AI::armyFormation::ArmyExchangeSide::SOURCE);
	const auto before = gameState()->saveToMemory();
	const auto rng = rngState();
	for(bool sameSlot : {false, true})
	{
		auto invalid = actual;
		invalid.transfers.front().withinReceiver = true;
		if(sameSlot)
		{
			invalid.transfers.front().sourceSide = NK2AI::armyFormation::ArmyExchangeSide::RECEIVER;
			invalid.transfers.front().destinationSlot = invalid.transfers.front().sourceSlot;
		}
		NK2AI::armyFormation::executeArmyExchange(callback, hero, town, invalid);
		EXPECT_EQ(gameState()->saveToMemory(), before);
		EXPECT_EQ(rngState(), rng);
		EXPECT_EQ(client->arrangements, 0);
		EXPECT_EQ(client->swaps, 0);
		checkAccepted();
	}
}

TEST_F(GarrisonPreparationTest, FullCurrentVisitorGoalConservesRissaArmyWithoutDismissingTroops)
{
	const CreatureID naga(CreatureID::decode("core:naga"));
	const CreatureID centaur(CreatureID::decode("core:centaur"));
	const CreatureID cabir(CreatureID::decode("core:masterGremlin"));
	ASSERT_NO_FATAL_FAILURE(prepare("core:rissa", 7,
		{{naga, 2}, {centaur, 19}, {cabir, 16}, {centaur, 19}, {cabir, 1}, {cabir, 1}, {naga, 2}},
		{{centaur, 33}}, false, FactionID::RAMPART));
	ASSERT_EQ(town->getFactionID(), FactionID::RAMPART);
	ASSERT_EQ(hero->visitablePos(), town->visitablePos());
	ASSERT_EQ(hero->getVisitedTown(), town);
	ASSERT_EQ(town->getVisitingHero(), hero);
	ASSERT_EQ(callback->getVisitableObjs(town->visitablePos()),
		(std::vector<const CGObjectInstance *>{town, hero}));
	ASSERT_EQ(map()->getTile(town->visitablePos()).visitableObjects,
		(std::vector<ObjectInstanceID>{town->id, hero->id}));
	ASSERT_FALSE(town->hasBuilt(BuildingID::BLACKSMITH));
	ASSERT_FALSE(town->hasBuilt(BuildingID::MAGES_GUILD_1));
	for(const CArmedInstance * army : {static_cast<const CArmedInstance *>(hero),
		static_cast<const CArmedInstance *>(town)})
		for(const auto & [slot, stack] : army->Slots())
		{
			UpgradeInfo upgrades(stack->getCreatureID());
			callback->fillUpgradeInfo(army, slot, upgrades);
			ASSERT_FALSE(upgrades.hasUpgrades());
		}
	ASSERT_EQ(NK2AI::townMusterArmyReward(gateway->nullkiller.get(), hero, town), 0);
	ASSERT_FALSE(NK2AI::armyFormation::canSwapGarrisonHero(town));
	checkAdmission(true);
	const auto initial = totals();
	ASSERT_EQ(initial.at(naga), 4);
	ASSERT_EQ(initial.at(centaur), 71);
	ASSERT_EQ(initial.at(cabir), 18);
	ASSERT_EQ(initial.at(naga) + initial.at(centaur) + initial.at(cabir), 93);
	const auto rng = rngState();
	const auto movement = hero->movementPointsRemaining();
	server->gateway = gateway.get();
	client->gateway = gateway.get();
	{
		std::shared_lock stateLock(CGameState::mutex);
		NK2AI::Goals::ExchangeSwapTownHeroes goal(town, hero, NK2AI::HeroLockedReason::DEFENCE);
		EXPECT_NO_THROW(goal.accept(gateway.get()));
	}
	EXPECT_EQ(town->getGarrisonHero(), hero);
	EXPECT_EQ(town->getVisitingHero(), nullptr);
	EXPECT_TRUE(gateway->nullkiller->isHeroLocked(hero));
	EXPECT_EQ(town->stacksCount(), 0);
	EXPECT_EQ(hero->stacksCount(), 7);
	EXPECT_EQ(client->moves, 1);
	EXPECT_EQ(client->arrangements, 4);
	EXPECT_EQ(client->swaps, 1);
	EXPECT_EQ(client->disbands, 0);
	EXPECT_EQ(totals(), initial);
	EXPECT_EQ(hero->movementPointsRemaining(), movement);
	EXPECT_EQ(rngState(), rng);
	checkAccepted();
	checkCapacities();
}
