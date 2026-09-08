/* Part of VCMI; GPL v2 or later, see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../hero/NewHorizonsLogisticsMasteryFixture.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/entities/hero/NewHorizonsMasteryEffects.h"
#include "../../../lib/pathfinder/PathfinderCache.h"
#include "../../../lib/pathfinder/CGPathNode.h"
#include "../../../lib/pathfinder/TurnInfo.h"
#include "../../../lib/mapping/TerrainTile.h"
#include "../../../lib/callback/CCallback.h"
#include "../../../server/queries/MapQueries.h"
#include "../../../server/queries/QueriesProcessor.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/gameState/TavernHeroesPool.h"
#include "../../../lib/gameState/CGameStateCampaign.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/rmg/CMapGenOptions.h"
#include "../../../lib/mapObjects/MiscObjects.h"
#include "../../../lib/mapObjects/ObjectTemplate.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/mapObjects/TownBuildingInstance.h"
#include "../../../lib/mapping/CCastleEvent.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/entities/building/TownFortifications.h"
#include "../../../lib/campaign/CampaignState.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "NewHorizonsLogisticsMasteryAILoopback.h"

using namespace newHorizonsHeroes;

class NewHorizonsLogisticsMasteryTest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * map) override
	{
		useCommands = false;
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode());
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_MASTERIES, logisticsMasteryRules());
	}

	void train(bool both = false)
	{
		for(SecondarySkill skill : {SecondarySkill::LOGISTICS, SecondarySkill::BALLISTICS,
			SecondarySkill::FIRST_AID, SecondarySkill::OFFENCE, SecondarySkill::ARMORER,
			SecondarySkill::SCOUTING, SecondarySkill::NAVIGATION, SecondarySkill::DIPLOMACY})
			gameHandler->changeSecSkill(attackerSideHero, skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		if(both)
		{
			gameHandler->changeSecSkill(attackerSideHero, SecondarySkill::DIPLOMACY, 0, ChangeValueMode::ABSOLUTE);
			gameHandler->changeSecSkill(attackerSideHero, SecondarySkill::ARTILLERY, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		}
		gameHandler->onAdvInterfaceReady(PlayerColor(0));
	}

	void level()
	{
		attackerSideHero->setExperience(LIBRARY->heroh->reqExp(attackerSideHero->level + 1), ChangeValueMode::ABSOLUTE);
		gameHandler->levelUpHero(attackerSideHero);
		const auto query = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(gameHandler->queries->topQuery(PlayerColor(0)));
		ASSERT_TRUE(query);
		ASSERT_TRUE(query->hlu.skills.empty());
		ASSERT_TRUE(gameHandler->queryReply(query->queryID, 0, PlayerColor(0)));
	}

	void choose(MasteryEffect effect)
	{
		const auto query = std::dynamic_pointer_cast<CHeroMasteryDialogQuery>(gameHandler->queries->topQuery(PlayerColor(0)));
		ASSERT_TRUE(query);
		ASSERT_TRUE(attackerSideHero->getMasteryState().pending);
		const auto offer = *attackerSideHero->getMasteryState().pending;
		const auto found = std::find_if(offer.options.begin(), offer.options.end(), [effect](const auto & option) { return option.effect == effect; });
		ASSERT_NE(found, offer.options.end());
		ASSERT_TRUE(gameHandler->heroMasteryReply(query->queryID, offer.hero, offer.sequence,
			static_cast<int>(found - offer.options.begin()), offer.player));
	}
};

TEST_F(NewHorizonsLogisticsMasteryTest, ActualQueriesDrainBothFamiliesBeforeFurtherLeveling)
{
	startGame();
	train(true);
	level();
	choose(MasteryEffect::ARTILLERY_VOLLEY);
	ASSERT_TRUE(attackerSideHero->getMasteryState().pending);
	EXPECT_EQ(attackerSideHero->getMasteryState().pending->skill, SecondarySkill::LOGISTICS);
	choose(MasteryEffect::LOGISTICS_FORCED_MARCH);
	EXPECT_FALSE(gameHandler->queries->topQuery(PlayerColor(0)));
	EXPECT_EQ(attackerSideHero->getMasteryState().selected.size(), 2);
	EXPECT_EQ(attackerSideHero->level, 2);
}

TEST_F(NewHorizonsLogisticsMasteryTest, ForcedMarchIsLandOnlyAndDormantUntilExpertReacquired)
{
	startGame();
	train();
	TurnInfoCache cache(attackerSideHero);
	const TurnInfo before(&cache, attackerSideHero, 0);
	const auto remaining = attackerSideHero->movementPointsRemaining();
	level();
	choose(MasteryEffect::LOGISTICS_FORCED_MARCH);
	const TurnInfo chosen(&cache, attackerSideHero, 0);
	EXPECT_EQ(chosen.getMovePointsLimitLand(), before.getMovePointsLimitLand() + 300);
	EXPECT_EQ(chosen.getMovePointsLimitWater(), before.getMovePointsLimitWater());
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), remaining) << "Choosing a mastery is not a movement refill";
	gameHandler->changeSecSkill(attackerSideHero, SecondarySkill::LOGISTICS, 0, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(attackerSideHero->getMasteryView()->choices.size(), 1);
	EXPECT_FALSE(attackerSideHero->getMasteryView()->choices.front().active);
	EXPECT_FALSE(attackerSideHero->hasBonus(Selector::type()(BonusType::MOVEMENT)
		.And(Selector::sourceType()(BonusSource::HERO_MASTERY))));
	gameHandler->changeSecSkill(attackerSideHero, SecondarySkill::LOGISTICS, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	const TurnInfo restored(&cache, attackerSideHero, 0);
	EXPECT_EQ(restored.getMovePointsLimitLand(), chosen.getMovePointsLimitLand());
	EXPECT_TRUE(attackerSideHero->getMasteryView()->choices.front().active);
	EXPECT_FALSE(attackerSideHero->prepareMasteryOffer());
}

TEST_F(NewHorizonsLogisticsMasteryTest, CachedTerrainPathRefreshesAndAuthoritativeMoveMatches)
{
	startGame();
	train();
	const auto source = attackerSideHero->visitablePos();
	const auto destination = source + int3(0, 1, 0);
	gameState()->getMap().getTile(source).terrainType = ETerrainId::SWAMP;
	gameState()->getMap().getTile(destination).terrainType = ETerrainId::SWAMP;
	level(); // Cache AFTER the unrelated primary/secondary level-up invalidation.
	PathfinderCache cache(gameState().get(), PathfinderOptions(*gameState()));
	const auto before = cache.getPathsInfo(attackerSideHero);
	const auto oldRemaining = before->getNode(destination)->moveRemains;
	ASSERT_EQ(before->getNode(destination)->turns, 0);
	ASSERT_EQ(cache.getPathsInfo(attackerSideHero).get(), before.get());
	choose(MasteryEffect::LOGISTICS_PATHFINDER);
	const auto after = cache.getPathsInfo(attackerSideHero);
	ASSERT_NE(after.get(), before.get());
	ASSERT_EQ(after->getNode(destination)->turns, 0);
	EXPECT_EQ(after->getNode(destination)->moveRemains, oldRemaining + 50);
	EXPECT_EQ(before->getNode(destination)->moveRemains, oldRemaining);
	const auto expected = after->getNode(destination)->moveRemains;
	ASSERT_TRUE(gameHandler->moveHero(attackerSideHero->id, attackerSideHero->convertFromVisitablePos(destination),
		EMovementMode::STANDARD, false, PlayerColor(0), EPathfindingLayer::LAND));
	EXPECT_EQ(attackerSideHero->visitablePos(), destination);
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), expected);
}

TEST_F(NewHorizonsLogisticsMasteryTest, QuartermasterRefreshesCachedBoardingAndExecutesBothTransitions)
{
	startGame();
	train();
	const auto source = attackerSideHero->visitablePos();
	const auto water = source + int3(0, 1, 0);
	gameState()->getMap().getTile(water).terrainType = ETerrainId::WATER;
	gameHandler->createBoat(water, BoatId::CASTLE, PlayerColor(0));
	level(); // Prime a valid cache at the pending mastery, not before leveling.
	PathfinderCache paths(gameState().get(), PathfinderOptions(*gameState()));
	const auto before = paths.getPathsInfo(attackerSideHero);
	ASSERT_EQ(before->getNode(water, EPathfindingLayer::SAIL)->turns, 0);
	EXPECT_EQ(before->getNode(water, EPathfindingLayer::SAIL)->moveRemains, 0);
	ASSERT_EQ(paths.getPathsInfo(attackerSideHero).get(), before.get());
	choose(MasteryEffect::LOGISTICS_QUARTERMASTER);
	const auto after = paths.getPathsInfo(attackerSideHero);
	ASSERT_NE(before.get(), after.get());
	const int expectedBoard = after->getNode(water, EPathfindingLayer::SAIL)->moveRemains;
	ASSERT_GT(expectedBoard, 0);
	TurnInfoCache limitsCache(attackerSideHero);
	const TurnInfo limits(&limitsCache, attackerSideHero, 0);
	const int boardByFormula = static_cast<int>((attackerSideHero->movementPointsRemaining() - limits.getMovementCostBase())
		* static_cast<double>(limits.getMovePointsLimitWater()) / limits.getMovePointsLimitLand());
	EXPECT_EQ(expectedBoard, boardByFormula);
	ASSERT_TRUE(gameHandler->moveHero(attackerSideHero->id, attackerSideHero->convertFromVisitablePos(water),
		EMovementMode::STANDARD, false, PlayerColor(0), EPathfindingLayer::SAIL));
	ASSERT_TRUE(attackerSideHero->inBoat());
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), expectedBoard);
	// Position changed through an authoritative move, so use a new path cache;
	// this does not pretend bonus-version invalidation handles position changes.
	PathfinderCache sailing(gameState().get(), PathfinderOptions(*gameState()));
	const auto landing = sailing.getPathsInfo(attackerSideHero);
	const int expectedLand = landing->getNode(source, EPathfindingLayer::LAND)->moveRemains;
	ASSERT_EQ(landing->getNode(source, EPathfindingLayer::LAND)->turns, 0);
	ASSERT_GT(expectedLand, 0);
	const int landByFormula = static_cast<int>((expectedBoard - limits.getMovementCostBase())
		* static_cast<double>(limits.getMovePointsLimitLand()) / limits.getMovePointsLimitWater());
	EXPECT_EQ(expectedLand, landByFormula);
	ASSERT_TRUE(gameHandler->moveHero(attackerSideHero->id, attackerSideHero->convertFromVisitablePos(source),
		EMovementMode::STANDARD, false, PlayerColor(0), EPathfindingLayer::LAND));
	EXPECT_FALSE(attackerSideHero->inBoat());
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), expectedLand);
}

TEST_F(NewHorizonsLogisticsMasteryTest, FullWorldSavePreservesDormantChoiceAndReacquisitionRestoresOnce)
{
	startGame();
	train();
	level();
	choose(MasteryEffect::LOGISTICS_FORCED_MARCH);
	TurnInfoCache cache(attackerSideHero);
	const int chosenLimit = TurnInfo(&cache, attackerSideHero, 0).getMovePointsLimitLand();
	gameHandler->changeSecSkill(attackerSideHero, SecondarySkill::LOGISTICS, 0, ChangeValueMode::ABSOLUTE);
	const auto heroID = attackerSideHero->id;
	const auto savedRules = attackerSideHero->getMasteryState().rules;
	CMemorySerializer memory;
	memory.oser & *gameState();
	auto restored = std::make_shared<CGameState>();
	memory.iser.cb = restored.get();
	memory.iser.loadingGamestate = true;
	memory.iser & *restored;
	const auto * hero = restored->getHero(heroID);
	ASSERT_NE(hero, nullptr);
	ASSERT_EQ(hero->getMasteryState().selected.size(), 1);
	EXPECT_EQ(hero->getMasteryState().rules, savedRules);
	EXPECT_FALSE(hero->getMasteryView()->choices.front().active);
	RecordingGameServer resumed;
	resumed.gameState = restored;
	CGameHandler handler(resumed, restored);
	handler.changeSecSkill(hero, SecondarySkill::LOGISTICS, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	TurnInfoCache loadedCache(hero);
	EXPECT_EQ(TurnInfo(&loadedCache, hero, 0).getMovePointsLimitLand(), chosenLimit);
	EXPECT_TRUE(hero->getMasteryView()->choices.front().active);
	EXPECT_FALSE(hero->prepareMasteryOffer());
	handler.changeSecSkill(hero, SecondarySkill::LOGISTICS, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(TurnInfo(&loadedCache, hero, 0).getMovePointsLimitLand(), chosenLimit);
}

TEST_F(NewHorizonsLogisticsMasteryTest, AIUsesVisibleGrassSwampCoastAndIgnoresHiddenWater)
{
	startGame();
	train();
	level();
	ASSERT_TRUE(attackerSideHero->getMasteryState().pending);
	auto offer = *attackerSideHero->getMasteryState().pending;
	CCallback visible(gameState(), PlayerColor(0), nullptr);
	CCallback foreign(gameState(), PlayerColor(1), nullptr);
	EXPECT_THROW(chooseMasteryForArmy(offer, *attackerSideHero), std::runtime_error);
	EXPECT_THROW(chooseMasteryForArmy(offer, *attackerSideHero, &foreign), std::runtime_error);
	const auto position = attackerSideHero->visitablePos();
	const auto paint = [&](ETerrainId terrain)
	{
		FoWChange reveal;
		reveal.player = PlayerColor(0);
		reveal.mode = ETileVisibility::REVEALED;
		for(int x = -2; x <= 2; ++x)
			for(int y = -2; y <= 2; ++y)
			{
				const auto tile = position + int3(x, y, 0);
				gameState()->getMap().getTile(tile).terrainType = terrain;
				reveal.tiles.insert(tile);
			}
		gameHandler->sendAndApply(reveal);
	};
	const auto selectedEffect = [&]() { return offer.options[chooseMasteryForArmy(offer, *attackerSideHero, &visible)].effect; };
	paint(ETerrainId::GRASS);
	EXPECT_EQ(selectedEffect(), MasteryEffect::LOGISTICS_FORCED_MARCH);
	paint(ETerrainId::SWAMP);
	EXPECT_EQ(selectedEffect(), MasteryEffect::LOGISTICS_PATHFINDER);
	paint(ETerrainId::GRASS);
	const auto coast = position + int3(0, 1, 0);
	gameState()->getMap().getTile(coast).terrainType = ETerrainId::WATER;
	ASSERT_NE(visible.getTile(coast, false), nullptr);
	EXPECT_EQ(selectedEffect(), MasteryEffect::LOGISTICS_QUARTERMASTER);
	std::reverse(offer.options.begin(), offer.options.end());
	EXPECT_EQ(selectedEffect(), MasteryEffect::LOGISTICS_QUARTERMASTER);
	FoWChange hide;
	hide.player = PlayerColor(0);
	hide.mode = ETileVisibility::HIDDEN;
	hide.tiles.insert(coast);
	gameHandler->sendAndApply(hide);
	// HIDDEN preserves owned observers' sight. This adjacent coast is still
	// legitimately visible; do not change production fog rules to hide it.
	ASSERT_NE(visible.getTile(coast, false), nullptr);
	EXPECT_EQ(selectedEffect(), MasteryEffect::LOGISTICS_QUARTERMASTER);

	// Give this fixture hero a valid zero-radius sight cap through a state
	// packet. Expert Scouting would otherwise immediately reveal the coast.
	GiveBonus sightCap;
	sightCap.id = attackerSideHero->id;
	sightCap.bonus.type = BonusType::SIGHT_RADIUS;
	sightCap.bonus.valType = BonusValueType::INDEPENDENT_MIN;
	sightCap.bonus.val = 0;
	gameHandler->sendAndApply(sightCap);
	ASSERT_EQ(attackerSideHero->getSightRadius(), 0);
	gameHandler->sendAndApply(hide);
	ASSERT_TRUE(gameState()->getMap().getTile(coast).isWater());
	ASSERT_EQ(visible.getTile(coast, false), nullptr);
	EXPECT_EQ(selectedEffect(), MasteryEffect::LOGISTICS_FORCED_MARCH);

	// Same terrain, offer and sight cap: restoring visibility must restore
	// the coastal choice. This also rejects a blanket "ignore all water" fix.
	FoWChange revealAgain = hide;
	revealAgain.mode = ETileVisibility::REVEALED;
	gameHandler->sendAndApply(revealAgain);
	ASSERT_NE(visible.getTile(coast, false), nullptr);
	EXPECT_EQ(selectedEffect(), MasteryEffect::LOGISTICS_QUARTERMASTER);
}

class NewHorizonsLogisticsMasteryAITest : public NewHorizonsLogisticsMasteryTest,
	public ::testing::WithParamInterface<int> {};

TEST_P(NewHorizonsLogisticsMasteryAITest, ActualNullkillerValuesVisibleContextAndSubmitsAuthoritativeChoice)
{
	startGame();
	train();
	const auto position = attackerSideHero->visitablePos();
	FoWChange reveal;
	reveal.player = PlayerColor(0);
	reveal.mode = ETileVisibility::REVEALED;
	for(int x = -2; x <= 2; ++x)
		for(int y = -2; y <= 2; ++y)
		{
			const auto tile = position + int3(x, y, 0);
			gameState()->getMap().getTile(tile).terrainType = GetParam() == 1 ? ETerrainId::SWAMP : ETerrainId::GRASS;
			reveal.tiles.insert(tile);
		}
	if(GetParam() == 2)
		gameState()->getMap().getTile(position + int3(0, 1, 0)).terrainType = ETerrainId::WATER;
	gameHandler->sendAndApply(reveal);
	level();
	ASSERT_TRUE(attackerSideHero->getMasteryState().pending);
	const auto offer = *attackerSideHero->getMasteryState().pending;
	const auto query = gameHandler->queries->topQuery(PlayerColor(0));
	ASSERT_TRUE(query);
	const auto expected = GetParam() == 0 ? MasteryEffect::LOGISTICS_FORCED_MARCH
		: GetParam() == 1 ? MasteryEffect::LOGISTICS_PATHFINDER : MasteryEffect::LOGISTICS_QUARTERMASTER;
	const auto transport = std::make_shared<LogisticsMasteryLoopback>(*gameHandler);
	const auto callback = std::make_shared<CCallback>(gameState(), PlayerColor(0), transport.get());
	const auto ai = AIFactory::createAdventureAI("Nullkiller2");
	transport->ai = ai;
	ai->initGameInterface(std::make_shared<LogisticsMasteryEnvironment>(gameState()), callback);
	auto result = transport->completed.get_future();
	ai->heroGotMastery(offer, query->queryID);
	const auto ready = result.wait_for(std::chrono::seconds(10));
	ai->finish();
	ASSERT_EQ(ready, std::future_status::ready);
	const auto [choice, accepted] = result.get();
	ASSERT_TRUE(accepted);
	EXPECT_EQ(offer.options.at(choice).effect, expected);
	ASSERT_EQ(attackerSideHero->getMasteryState().selected.size(), 1);
	EXPECT_EQ(attackerSideHero->getMasteryState().selected.front().option.effect, expected);
	EXPECT_FALSE(gameHandler->queries->topQuery(PlayerColor(0)));
}

INSTANTIATE_TEST_SUITE_P(VisibleMovementContexts, NewHorizonsLogisticsMasteryAITest, ::testing::Values(0, 1, 2));

TEST_F(NewHorizonsLogisticsMasteryTest, TerrainDiscountClampsAtBaseAndNeverMakesRockPassable)
{
	startGame();
	train();
	const auto source = attackerSideHero->visitablePos();
	const auto destination = source + int3(0, 1, 0);
	const auto rock = source + int3(0, 2, 0);
	gameState()->getMap().getTile(source).terrainType = ETerrainId::SAND;
	gameState()->getMap().getTile(destination).terrainType = ETerrainId::SAND;
	gameState()->getMap().getTile(rock).terrainType = ETerrainId::ROCK;
	level();
	choose(MasteryEffect::LOGISTICS_PATHFINDER);
	// Stack a legitimate Pathfinding skill discount with the mastery; it must
	// still bottom out at the ordinary base cost, never negative movement cost.
	gameHandler->changeSecSkill(attackerSideHero, SecondarySkill::SCOUTING, 0, ChangeValueMode::ABSOLUTE);
	gameHandler->changeSecSkill(attackerSideHero, SecondarySkill::PATHFINDING, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	TurnInfoCache movementCache(attackerSideHero);
	const TurnInfo movement(&movementCache, attackerSideHero, 0);
	const auto before = attackerSideHero->movementPointsRemaining();
	PathfinderCache paths(gameState().get(), PathfinderOptions(*gameState()));
	const auto route = paths.getPathsInfo(attackerSideHero);
	ASSERT_EQ(route->getNode(destination)->turns, 0);
	EXPECT_EQ(route->getNode(destination)->moveRemains, before - movement.getMovementCostBase());
	ASSERT_TRUE(gameHandler->moveHero(attackerSideHero->id, attackerSideHero->convertFromVisitablePos(destination),
		EMovementMode::STANDARD, false, PlayerColor(0), EPathfindingLayer::LAND));
	const auto after = attackerSideHero->movementPointsRemaining();
	EXPECT_EQ(after, before - movement.getMovementCostBase());
	EXPECT_FALSE(gameHandler->moveHero(attackerSideHero->id, attackerSideHero->convertFromVisitablePos(rock),
		EMovementMode::STANDARD, false, PlayerColor(0), EPathfindingLayer::LAND));
	EXPECT_EQ(attackerSideHero->visitablePos(), destination);
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), after);
}
