/*
 * NewHorizonsPeacemakerDangerAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "AI/Nullkiller2/AIGateway.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "AI/Nullkiller2/Goals/ExploreNeighbourTile.h"
#include "lib/CPlayerState.h"
#include "lib/CSkillHandler.h"
#include "lib/IGameSettings.h"
#include "lib/callback/CCallback.h"
#include "lib/callback/IClient.h"
#include "lib/entities/creature/NewHorizonsMusterRules.h"
#include "lib/gameState/CGameState.h"
#include "lib/mapObjects/CGCreature.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/serializer/CMemorySerializer.h"
#include "mock/GameHandlerTestServer.h"
#include "mock/TinyH3MBuilder.h"
#include "nullkiller2/NullkillerTest.h"
#include "server/CGameHandler.h"

namespace
{
constexpr PlayerColor PLAYER(0);
constexpr auto DIPLOMACY = "new-horizons:diplomacy";
constexpr auto PEACEMAKER = "new-horizons:diplomacy.peacemaker";
constexpr auto TRIBUTE = "new-horizons:diplomacy.tribute";

class PassageLoopback final : public IClient
{
	CGameHandler & handler;
	int lastRequest = 0;
public:
	std::vector<MoveHero> movements;
	explicit PassageLoopback(CGameHandler & handler) : handler(handler) {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor,
		const BattleID &, const BattleStateInfoForRetreat &) override { return std::nullopt; }
	int sendRequest(const CPackForServer & request, PlayerColor player, bool) override
	{
		const auto * move = dynamic_cast<const MoveHero *>(&request);
		if(!move)
			throw std::runtime_error("Peacemaker fixture expected a normal hero movement request");
		movements.push_back(*move);
		auto incoming = CMemorySerializer::deepCopy(request);
		incoming->player = player;
		incoming->requestID = ++lastRequest;
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, *incoming);
		return lastRequest;
	}
};

class NewHorizonsPeacemakerDangerAITest : public NullkillerTest
{
protected:
	CGHeroInstance * hero = nullptr;
	CGHeroInstance * otherHero = nullptr;
	CGCreature * neutral = nullptr;
	std::unique_ptr<GameHandlerTestServer> server;
	std::unique_ptr<CGameHandler> handler;

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare()
	{
		const auto pikeman = CreatureID(CreatureID::decode("core:pikeman"));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER)
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PLAYER)
			.heroGarrison({{pikeman, 1}})
			.hero({24, 24, 0}, HeroTypeID(HeroTypeID::decode("core:adela")), PLAYER)
			.monster({6, 4, 0}, pikeman, 20, static_cast<int8_t>(CGCreature::Character::HOSTILE));
		startWithMap(std::move(builder));
		hero = findHeroAt({5, 5, 0});
		otherHero = findHeroAt({24, 24, 0});
		neutral = findFirst<CGCreature>();
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(otherHero, nullptr);
		ASSERT_NE(neutral, nullptr);
		server = std::make_unique<GameHandlerTestServer>(gameState(), PLAYER);
		handler = std::make_unique<CGameHandler>(*server, gameState());
		gameState()->actingPlayers.insert(PLAYER);
		const auto skill = SecondarySkill(SecondarySkill::decode(DIPLOMACY));
		handler->levelUpHero(hero, skill, false);
		ASSERT_TRUE(selectPerk(PEACEMAKER, MasteryLevel::BASIC));
		handler->levelUpHero(hero, skill, false);
		ASSERT_TRUE(selectPerk(TRIBUTE, MasteryLevel::ADVANCED));
		neutral->agression = 10;
		neutral->neverFlees = true;
		ASSERT_FALSE(neutral->getNewHorizonsDiplomacyForecast(*hero).willing);
		revealMap(PLAYER);
	}

	bool selectPerk(const std::string & perk, MasteryLevel::Type rank)
	{
		const auto lookup = [this](const std::string & skill) { return hero->getPerkSkillRank(skill); };
		for(uint64_t seed = 0; seed < 10000; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(lookup, seed);
			for(size_t index = 0; index < offer.size(); ++index)
				if(offer[index].selection.skillId == DIPLOMACY && offer[index].selection.perkId == perk
					&& offer[index].requiredRank == rank)
				{
					handler->levelUpHero(hero, offer, index, seed, false);
					return hero->hasActivePerk(DIPLOMACY, perk);
				}
		}
		return false;
	}

	std::optional<int3> adjacentGuardedTile() const
	{
		const auto source = hero->visitablePos();
		for(int dx = -1; dx <= 1; ++dx)
			for(int dy = -1; dy <= 1; ++dy)
			{
				const auto tile = source + int3(dx, dy, 0);
				if((dx || dy) && map()->isInTheMap(tile) && tile != neutral->visitablePos()
					&& map()->guardingCreaturePosition(tile) == neutral->visitablePos()
					&& !map()->getTile(tile).blocked())
					return tile;
			}
		return std::nullopt;
	}

	void triggerPassage()
	{
		const auto approach = adjacentGuardedTile();
		ASSERT_TRUE(approach);
		handler->setMovePoints(hero->id, 20000);
		ASSERT_TRUE(handler->moveHero(hero->id, hero->convertFromVisitablePos(*approach),
			EMovementMode::STANDARD, false, PLAYER, EPathfindingLayer::LAND));
		ASSERT_EQ(hero->visitablePos(), *approach);
		ASSERT_TRUE(neutral->passableFor(hero));
		ASSERT_EQ(gameState()->getBattle(PLAYER), nullptr);
	}
};
}

TEST_F(NewHorizonsPeacemakerDangerAITest, ProtectedHeroHasZeroAdjacentGuardDangerButOtherHeroAndDirectAttackDoNot)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto before = adjacentGuardedTile();
	ASSERT_TRUE(before);
	auto gateway = makeGateway(PLAYER);
	EXPECT_GT(gateway->nullkiller->dangerEvaluator->evaluateDanger(*before, hero), 0u);
	ASSERT_NO_FATAL_FAILURE(triggerPassage());
	const auto guarded = adjacentGuardedTile();
	ASSERT_TRUE(guarded);
	EXPECT_EQ(gateway->nullkiller->dangerEvaluator->evaluateDanger(*guarded, hero), 0u);
	EXPECT_GT(gateway->nullkiller->dangerEvaluator->evaluateDanger(*guarded, otherHero), 0u);
	EXPECT_GT(gateway->nullkiller->dangerEvaluator->evaluateDanger(*guarded, nullptr), 0u);
	EXPECT_GT(gateway->nullkiller->dangerEvaluator->evaluateDanger(neutral->visitablePos(), hero), 0u)
		<< "Deliberately visiting the creature still attacks; objectDanger must not disappear";
}

TEST_F(NewHorizonsPeacemakerDangerAITest, ProtectionExpiryRestoresDangerWithoutChangingNeutralArmy)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(triggerPassage());
	const auto guarded = adjacentGuardedTile();
	ASSERT_TRUE(guarded);
	auto gateway = makeGateway(PLAYER);
	ASSERT_EQ(gateway->nullkiller->dangerEvaluator->evaluateDanger(*guarded, hero), 0u);
	const auto strength = neutral->getArmyStrength();
	const auto calendar = gameState()->getCalendar();
	const auto week = newHorizonsMuster::absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek());
	gameState()->day = (week + 1) * calendar.getDaysInWeek() + 1;
	const auto nextCalendar = gameState()->getCalendar();
	ASSERT_NE(newHorizonsMuster::absoluteWeek(nextCalendar.getCurrentDay(), nextCalendar.getDaysInWeek()), week);
	EXPECT_FALSE(neutral->passableFor(hero));
	EXPECT_GT(gateway->nullkiller->dangerEvaluator->evaluateDanger(*guarded, hero), 0u);
	EXPECT_EQ(neutral->getArmyStrength(), strength);
}

TEST_F(NewHorizonsPeacemakerDangerAITest, ActualNeighbourSelectorAndMovementRequestCrossProtectedAreaWithoutCombat)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(triggerPassage());
	const auto guarded = adjacentGuardedTile();
	ASSERT_TRUE(guarded);
	const auto source = hero->visitablePos();
	ASSERT_EQ(std::abs(guarded->x - source.x) + std::abs(guarded->y - source.y), 1)
		<< "The isolated route must not depend on moving diagonally across impassable corners";
	// Isolate one normal neighbouring passage endpoint. This is map geometry,
	// not a fake danger result or bypass of the production exploration selector.
	for(int dx = -1; dx <= 1; ++dx)
		for(int dy = -1; dy <= 1; ++dy)
		{
			const auto tile = source + int3(dx, dy, 0);
			if((dx || dy) && tile != *guarded && tile != neutral->visitablePos() && map()->isInTheMap(tile))
				map()->getTile(tile).terrainType = ETerrainId::ROCK;
		}
	setMapVisibility(PLAYER, false);
	auto * team = gameState()->getPlayerTeam(PLAYER);
	team->fogOfWarMap[source] = 1;
	team->fogOfWarMap[*guarded] = 1;
	team->fogOfWarMap[neutral->visitablePos()] = 1;
	PassageLoopback client(*handler);
	auto callback = makeCallback(PLAYER, &client);
	auto gateway = makeGateway(callback);
	const auto selected = NK2AI::Goals::ExploreNeighbourTile::findTarget(hero, gateway->nullkiller.get());
	ASSERT_TRUE(selected);
	ASSERT_EQ(selected->tile, *guarded);
	ASSERT_GT(selected->tilesDiscovered, 0);
	const auto neutralStrength = neutral->getArmyStrength();
	callback->moveHero(hero, hero->convertFromVisitablePos(selected->tile), false, EPathfindingLayer::LAND);
	ASSERT_EQ(client.movements.size(), 1u);
	EXPECT_EQ(client.movements.front().hid, hero->id);
	EXPECT_EQ(hero->visitablePos(), selected->tile);
	EXPECT_EQ(gameState()->getBattle(PLAYER), nullptr);
	EXPECT_EQ(neutral->getArmyStrength(), neutralStrength);
	EXPECT_TRUE(neutral->passableFor(hero));
}
