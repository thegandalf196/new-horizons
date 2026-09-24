/*
 * GatherArmyBehaviorTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "AI/Nullkiller2/AIGateway.h"
#include "AI/Nullkiller2/Behaviors/GatherArmyBehavior.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "AI/Nullkiller2/Helpers/ArmyFormation.h"
#include "AI/Nullkiller2/Pathfinding/AIPathfinder.h"

#include "mock/GameHandlerTestServer.h"
#include "mock/TinyH3MBuilder.h"
#include "nullkiller2/NullkillerTest.h"

#include "server/CGameHandler.h"

#include "lib/callback/CCallback.h"
#include "lib/callback/IClient.h"
#include "lib/gameState/CGameState.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/CGTownInstance.h"
#include "lib/networkPacks/PacksForClient.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/serializer/CMemorySerializer.h"

namespace
{
const PlayerColor PLAYER(0);
const PlayerColor ENEMY(1);
const int3 EXCHANGE_RECEIVER_POS(5, 5, 0);
const int3 EXCHANGE_SOURCE_POS(6, 5, 0);

class GameHandlerClient : public IClient
{
public:
	GameHandlerClient(const std::shared_ptr<CGameState> & gameState, PlayerColor player)
		: server(gameState, player)
		, gameHandler(server, gameState)
	{
		gameState->actingPlayers.insert(player);
	}

	std::optional<BattleAction> makeSurrenderRetreatDecision(
		PlayerColor,
		const BattleID &,
		const BattleStateInfoForRetreat &) override
	{
		return std::nullopt;
	}

	int sendRequest(const CPackForServer & request, PlayerColor player, bool) override
	{
		request.player = player;
		request.requestID = ++lastRequestID;
		auto serverRequest = CMemorySerializer::deepCopy(request);
		gameHandler.handleReceivedPack(
			GameConnectionID::FIRST_CONNECTION,
			*serverRequest);
		return lastRequestID;
	}

	int requestCount() const
	{
		return lastRequestID;
	}

	void openHeroExchange(ObjectInstanceID first, ObjectInstanceID second)
	{
		gameHandler.heroExchange(first, second);
	}

private:
	GameHandlerTestServer server;
	CGameHandler gameHandler;
	int lastRequestID = 0;
};

TinyH3M::TinyH3MBuilder makeGarrisonUpgradeMap()
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder
		.size(36, false)
		.name("NK2GarrisonUpgrade")
		.playerActive(PLAYER)
		.playerActive(ENEMY)
		.town({9, 5, 0}, FactionID::CASTLE, PLAYER)
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("orrin")), PLAYER)
		.town({30, 30, 0}, FactionID::CASTLE, ENEMY);

	return builder;
}

TinyH3M::TinyH3MBuilder makeLeadershipExchangeMap(CreatureID creature, uint16_t sourceCount)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder
		.size(36, false)
		.name("NK2LeadershipExchange")
		.playerActive(PLAYER)
		.town({30, 30, 0}, FactionID::CASTLE, PLAYER)
		.hero(EXCHANGE_RECEIVER_POS,
			HeroTypeID(HeroTypeID::decode("core:orrin")), PLAYER)
		.hero(EXCHANGE_SOURCE_POS,
			HeroTypeID(HeroTypeID::decode("core:valeska")), PLAYER)
		.heroGarrison({{creature, sourceCount}});

	return builder;
}

int totalCreatures(const CCreatureSet & army)
{
	int result = 0;
	for(const auto & [slot, stack] : army.Slots())
		result += stack->getCount();
	return result;
}

void updateHeroPaths(NK2AI::Nullkiller * ai)
{
	ai->heroManager->update();
	NK2AI::PathfinderSettings settings;
	settings.useHeroChain = true;
	ai->pathfinder->updatePaths(ai->getHeroesForPathfinding(), settings);
}

class Nullkiller2_Behaviors_GatherArmyBehavior : public NullkillerTest
{
protected:
	enum class CapabilityRules
	{
		INHERIT,
		LEGACY,
		NEW_HORIZONS
	};

	CapabilityRules capabilityRules = CapabilityRules::INHERIT;

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		if(capabilityRules == CapabilityRules::LEGACY)
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode());
		else if(capabilityRules == CapabilityRules::NEW_HORIZONS)
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
				JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
	}

	void startLegacyMap(TinyH3M::TinyH3MBuilder builder)
	{
		capabilityRules = CapabilityRules::LEGACY;
		startWithMap(std::move(builder));
		capabilityRules = CapabilityRules::INHERIT;
	}

	void startNewHorizonsMap(TinyH3M::TinyH3MBuilder builder)
	{
		capabilityRules = CapabilityRules::NEW_HORIZONS;
		startWithMap(std::move(builder));
		capabilityRules = CapabilityRules::INHERIT;
	}

	void putHeroInGarrison(const CGHeroInstance & hero, const CGTownInstance & town) const
	{
		ChangeObjPos moveHero;
		moveHero.objid = hero.id;
		moveHero.nPos = town.visitablePos();
		moveHero.initiator = PLAYER;
		gameState()->apply(moveHero);

		SetHeroesInTown setHeroes;
		setHeroes.tid = town.id;
		setHeroes.visiting = ObjectInstanceID::NONE;
		setHeroes.garrison = hero.id;
		gameState()->apply(setHeroes);
	}
};
}

TEST_F(Nullkiller2_Behaviors_GatherArmyBehavior, upgradesPikemenCarriedByGarrisonHero)
{
	startLegacyMap(makeGarrisonUpgradeMap());

	auto * hero = findHeroByOwner(PLAYER);
	auto * town = findFirst<CGTownInstance>();
	ASSERT_NE(hero, nullptr);
	ASSERT_NE(town, nullptr);
	ASSERT_EQ(town->getFactionID(), FactionID::CASTLE);

	const CreatureID pikeman(CreatureID::decode("pikeman"));
	const CreatureID halberdier(CreatureID::decode("halberdier"));
	town->addBuilding(BuildingID::DWELL_LVL_1_UP);
	town->creatures.at(0).second.push_back(halberdier);
	hero->clearSlots();
	ASSERT_TRUE(hero->setCreature(SlotID(0), pikeman, 1000));
	putHeroInGarrison(*hero, *town);
	grantResources(PLAYER, GameResID(GameResID::GOLD), 1000000);

	GameHandlerClient client(gameState(), PLAYER);
	const auto gateway = makeGateway(PLAYER, &client);
	gateway->nullkiller->makeTurn();

	ASSERT_NE(hero->getStackPtr(SlotID(0)), nullptr);
	EXPECT_EQ(hero->getStackPtr(SlotID(0))->getCreatureID(), halberdier);
}

TEST_F(Nullkiller2_Behaviors_GatherArmyBehavior, DoesNotGenerateGatherTaskWhenExchangeHasNoLeadershipCapacity)
{
	const CreatureID blackKnight(CreatureID::decode("core:blackKnight"));
	startNewHorizonsMap(makeLeadershipExchangeMap(blackKnight, 2));

	auto * receiver = findHeroAt(EXCHANGE_RECEIVER_POS);
	auto * source = findHeroAt(EXCHANGE_SOURCE_POS);
	ASSERT_NE(receiver, nullptr);
	ASSERT_NE(source, nullptr);
	receiver->clearSlots();
	source->clearSlots();
	ASSERT_TRUE(source->setCreature(SlotID(0), blackKnight, 2));
	const auto capacity = receiver->getLeadershipSlotCapacity(blackKnight);
	ASSERT_TRUE(capacity);
	ASSERT_GT(capacity->maximum, 0);
	const auto sourceCapacity = source->getLeadershipSlotCapacity(blackKnight);
	ASSERT_TRUE(sourceCapacity);
	ASSERT_GE(sourceCapacity->maximum, 2);
	for(size_t i = 0; i < GameConstants::ARMY_SIZE - 1; ++i)
		ASSERT_TRUE(receiver->setCreature(SlotID(i), blackKnight, 1));

	const auto gateway = makeGateway(PLAYER);
	const auto * ai = gateway->nullkiller.get();
	updateHeroPaths(gateway->nullkiller.get());

	const auto paths = ai->pathfinder->getPathInfo(receiver->visitablePos(), ai->isObjectGraphAllowed());
	const auto isSourcePath = [source](const NK2AI::AIPath & path)
	{
		return path.targetHero == source;
	};
	ASSERT_TRUE(std::ranges::any_of(paths, isSourcePath));

	NK2AI::Goals::GatherArmyBehavior gatherArmy;
	ASSERT_GT(blackKnight.toCreature()->getAIValue(), 500);
	EXPECT_EQ(
		ai->armyManager->howManyReinforcementsCanGet(receiver, source),
		blackKnight.toCreature()->getAIValue());
	EXPECT_FALSE(gatherArmy.decompose(ai).empty());

	for(size_t i = 0; i < GameConstants::ARMY_SIZE; ++i)
		ASSERT_TRUE(receiver->setCreature(SlotID(i), blackKnight, capacity->maximum));
	ASSERT_EQ(receiver->stacksCount(), GameConstants::ARMY_SIZE);

	const auto cappedGateway = makeGateway(PLAYER);
	const auto * cappedAi = cappedGateway->nullkiller.get();
	updateHeroPaths(cappedGateway->nullkiller.get());
	const auto cappedPaths = cappedAi->pathfinder->getPathInfo(receiver->visitablePos(), cappedAi->isObjectGraphAllowed());
	ASSERT_TRUE(std::ranges::any_of(cappedPaths, isSourcePath));
	EXPECT_EQ(cappedAi->armyManager->howManyReinforcementsCanGet(receiver, source), 0);
	EXPECT_TRUE(gatherArmy.decompose(cappedAi).empty());
}

TEST_F(Nullkiller2_Behaviors_GatherArmyBehavior, PickBestCreaturesAppliesProjectedTransferAndIsIdempotent)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	startNewHorizonsMap(makeLeadershipExchangeMap(pikeman, 4));

	auto * receiver = findHeroAt(EXCHANGE_RECEIVER_POS);
	auto * source = findHeroAt(EXCHANGE_SOURCE_POS);
	ASSERT_NE(receiver, nullptr);
	ASSERT_NE(source, nullptr);
	receiver->clearSlots();
	source->clearSlots();
	ASSERT_TRUE(source->setCreature(SlotID(0), pikeman, 4));
	const auto capacity = receiver->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(capacity);
	ASSERT_GT(capacity->maximum, 1);
	const auto sourceCapacity = source->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(sourceCapacity);
	ASSERT_GE(sourceCapacity->maximum, 4);
	for(size_t i = 0; i < GameConstants::ARMY_SIZE; ++i)
		ASSERT_TRUE(receiver->setCreature(
			SlotID(i), pikeman, capacity->maximum - (i + 1 == GameConstants::ARMY_SIZE ? 1 : 0)));
	ASSERT_EQ(receiver->stacksCount(), GameConstants::ARMY_SIZE);

	const int initialCreatures = totalCreatures(*receiver) + totalCreatures(*source);
	GameHandlerClient client(gameState(), PLAYER);
	const auto gateway = makeGateway(PLAYER, &client);
	client.openHeroExchange(receiver->id, source->id);
	NK2AI::armyFormation::ArmyExchangeProjection valuedPlan;
	const auto valuedArmy = gateway->nullkiller->armyManager->getBestArmy(
		receiver, receiver, source, TerrainId::NONE, source, &valuedPlan);
	ASSERT_EQ(valuedPlan.transfers.size(), 1);
	EXPECT_EQ(valuedPlan.transfers.front().transferCount, 1);
	EXPECT_EQ(valuedPlan.receiverSlots.back().count, capacity->maximum);
	EXPECT_EQ(valuedPlan.sourceSlots.front().count, 3);
	ASSERT_EQ(valuedArmy.size(), valuedPlan.receiverSlots.size());

	const int beforeFirstExchangeRequests = client.requestCount();
	gateway->pickBestCreatures(receiver, source);
	EXPECT_EQ(client.requestCount(), beforeFirstExchangeRequests + 1);
	EXPECT_EQ(receiver->getStackCount(SlotID(GameConstants::ARMY_SIZE - 1)), capacity->maximum);
	EXPECT_EQ(source->getStackCount(SlotID(0)), 3);
	EXPECT_EQ(totalCreatures(*receiver) + totalCreatures(*source), initialCreatures);
	for(size_t i = 0; i < GameConstants::ARMY_SIZE; ++i)
	{
		EXPECT_LE(receiver->getStackCount(SlotID(i)), capacity->maximum);
		const auto expectedReceiver = std::ranges::find_if(valuedPlan.receiverSlots,
			[i](const auto & stack) { return stack.slot == SlotID(i); });
		const auto * actualReceiver = receiver->getStackPtr(SlotID(i));
		if(expectedReceiver == valuedPlan.receiverSlots.end())
			EXPECT_EQ(actualReceiver, nullptr);
		else
		{
			ASSERT_NE(actualReceiver, nullptr);
			EXPECT_EQ(actualReceiver->getCreatureID(), expectedReceiver->creature);
			EXPECT_EQ(actualReceiver->getCount(), expectedReceiver->count);
		}

		const auto expectedSource = std::ranges::find_if(valuedPlan.sourceSlots,
			[i](const auto & stack) { return stack.slot == SlotID(i); });
		const auto * actualSource = source->getStackPtr(SlotID(i));
		if(expectedSource == valuedPlan.sourceSlots.end())
			EXPECT_EQ(actualSource, nullptr);
		else
		{
			ASSERT_NE(actualSource, nullptr);
			EXPECT_EQ(actualSource->getCreatureID(), expectedSource->creature);
			EXPECT_EQ(actualSource->getCount(), expectedSource->count);
		}
	}

	const int afterFirstExchangeRequests = client.requestCount();
	gateway->pickBestCreatures(receiver, source);
	EXPECT_EQ(client.requestCount(), afterFirstExchangeRequests);
	EXPECT_EQ(receiver->getStackCount(SlotID(GameConstants::ARMY_SIZE - 1)), capacity->maximum);
	EXPECT_EQ(source->getStackCount(SlotID(0)), 3);
	EXPECT_EQ(totalCreatures(*receiver) + totalCreatures(*source), initialCreatures);
}
