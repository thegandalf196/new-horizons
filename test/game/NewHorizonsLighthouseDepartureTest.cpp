/*
 * NewHorizonsLighthouseDepartureTest.cpp, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt.
 */
#include "NewHorizonsLighthouseDepartureFixture.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../client/windows/NewHorizonsBuildingVisitHelp.h"
#include "../../lib/texts/CGeneralTextHandler.h"

TEST_F(NewHorizonsLighthouseDepartureTest, BuildingHelpUsesCapturedDeparturePolicyAndKeepsLegacyFallback)
{
	ASSERT_NO_FATAL_FAILURE(start());
	const BuildingTypeUniqueID lighthouse(FactionID::CASTLE, BuildingID::SPECIAL_1);
	const auto describe = [&](const JsonNode & rules, const JsonNode & capabilities)
	{
		return newHorizonsBuildingVisitHelp::lighthouseDescription(lighthouse, rules, capabilities);
	};
	const auto & rules = hero->getPrimaryGrowthRules();
	const auto & capabilities = gameState()->getHeroCapabilityRules();
	const auto text = describe(rules, capabilities).toString(LIBRARY->generaltexth.get());
	EXPECT_NE(text.find("+20% sea movement"), std::string::npos);
	EXPECT_NE(text.find("no embarkation movement penalty"), std::string::npos);
	EXPECT_NE(text.find("that day only"), std::string::npos);
	EXPECT_EQ(text.find("500"), std::string::npos);
	auto changed = rules;
	changed["lighthouseDeparture"]["seaMovementPercent"].Integer() = 35;
	EXPECT_NE(describe(changed, capabilities).toString(LIBRARY->generaltexth.get()).find("+35%"), std::string::npos);
	auto absent = rules;
	absent.Struct().erase("lighthouseDeparture");
	EXPECT_TRUE(describe(absent, capabilities).empty());
	changed["lighthouseDeparture"]["enabled"].Bool() = false;
	EXPECT_TRUE(describe(changed, capabilities).empty());
	EXPECT_TRUE(describe(rules, JsonNode()).empty());
	EXPECT_TRUE(newHorizonsBuildingVisitHelp::lighthouseDescription(
		BuildingTypeUniqueID(FactionID::TOWER, BuildingID::SPECIAL_1), rules, capabilities).empty());
	EXPECT_TRUE(newHorizonsBuildingVisitHelp::lighthouseDescription(
		BuildingTypeUniqueID(FactionID::CASTLE, BuildingID::SHIPYARD), rules, capabilities).empty());
}

TEST_F(NewHorizonsLighthouseDepartureTest, AuthoredRulesResolveAndCaptureDepartureWithoutInventingAbsentFeature)
{
	start();
	const auto authored = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
	ASSERT_NO_THROW(newHorizonsHeroes::validateHeroRules(authored, true));
	const auto resolved = newHorizonsHeroes::resolveHeroRules(authored, hero->getHeroClassID());
	ASSERT_NO_THROW(newHorizonsHeroes::validateResolvedHeroRules(resolved));
	EXPECT_EQ(resolved["lighthouseDeparture"], authored["lighthouseDeparture"]);
	EXPECT_EQ(hero->getPrimaryGrowthRules()["lighthouseDeparture"], authored["lighthouseDeparture"]);
	EXPECT_EQ(newHorizonsLighthouse::departurePercent(*hero), 20);
	auto absent = authored;
	absent.Struct().erase("lighthouseDeparture");
	ASSERT_NO_THROW(newHorizonsHeroes::validateHeroRules(absent, true));
	EXPECT_FALSE(newHorizonsHeroes::resolveHeroRules(absent, hero->getHeroClassID()).Struct().contains("lighthouseDeparture"));
	auto malformed = authored;
	malformed["lighthouseDeparture"]["seaMovementPercent"].Float() = 20.5;
	EXPECT_THROW(newHorizonsHeroes::validateHeroRules(malformed, true), std::runtime_error);
	malformed = resolved;
	malformed["lighthouseDeparture"]["enabled"].String() = "true";
	EXPECT_THROW(newHorizonsHeroes::validateResolvedHeroRules(malformed), std::runtime_error);
}

TEST_F(NewHorizonsLighthouseDepartureTest, ActualOwnedPortEmbarkMatchesCoreForecastAndCapturedSeaBonus)
{
	ASSERT_NO_FATAL_FAILURE(start());
	ASSERT_EQ(newHorizonsLighthouse::departureTown(*hero, port), town);
	const int before = hero->movementPointsRemaining();
	const auto seaBefore = hero->getTurnInfo(0)->getMaxMovePoints(EPathfindingLayer::SAIL);
	PathfinderCache cache(gameState().get(), PathfinderOptions(*gameState()));
	const auto paths = cache.getPathsInfo(hero);
	const auto * predicted = paths->getNode(port, EPathfindingLayer::SAIL);
	ASSERT_TRUE(predicted->reachable());
	ASSERT_EQ(predicted->turns, 0);
	const int expected = receipt().movePoints;
	ASSERT_EQ(predicted->moveRemains, expected);
	ASSERT_TRUE(handler->moveHero(hero->id, hero->convertFromVisitablePos(port),
		EMovementMode::STANDARD, false, hero->getOwner(), EPathfindingLayer::SAIL));
	EXPECT_EQ(hero->movementPointsRemaining(), expected);
	EXPECT_GT(expected, 0);
	EXPECT_TRUE(hero->inBoat());
	EXPECT_TRUE(newHorizonsLighthouse::hasDepartureBonus(*hero));
	EXPECT_EQ(seaBefore, hero->getTurnInfo(1)->getMaxMovePoints(EPathfindingLayer::SAIL));
	EXPECT_EQ(hero->getTurnInfo(0)->getMaxMovePoints(EPathfindingLayer::SAIL),
		hero->getTurnInfo(0)->getLighthouseSeaMovePoints());
	EXPECT_GT(hero->getTurnInfo(0)->getMaxMovePoints(EPathfindingLayer::SAIL), seaBefore);
	EXPECT_EQ(before, paths->getNode(shore, EPathfindingLayer::LAND)->moveRemains);
}

TEST_F(NewHorizonsLighthouseDepartureTest, SavedAbsenceRetainsExistingV3Castle500AndOrdinaryEmbarkPenalty)
{
	ASSERT_NO_FATAL_FAILURE(start(false));
	ASSERT_TRUE(hero->usesNewHorizonsMovement());
	EXPECT_EQ(newHorizonsLighthouse::departurePercent(*hero), 0);
	EXPECT_EQ(newHorizonsLighthouse::departureTown(*hero, port), nullptr);
	EXPECT_EQ(hero->getTurnInfo(0)->getMaxMovePoints(EPathfindingLayer::SAIL),
		newHorizonsMovement::BASE_DAILY_MOVEMENT + 500);
	ASSERT_TRUE(handler->moveHero(hero->id, hero->convertFromVisitablePos(port),
		EMovementMode::STANDARD, false, hero->getOwner(), EPathfindingLayer::SAIL));
	EXPECT_EQ(hero->movementPointsRemaining(), 0);
	EXPECT_FALSE(newHorizonsLighthouse::hasDepartureBonus(*hero));
}

TEST_F(NewHorizonsLighthouseDepartureTest, NonPortBoatDoesNotBenefitFromAnotherOwnedLighthouse)
{
	ASSERT_NO_FATAL_FAILURE(start());
	const int3 other = shore + int3(-1, 0, 0);
	map()->getTile(other).terrainType = ETerrainId::WATER;
	handler->createBoat(other, BoatId::CASTLE, hero->getOwner());
	EXPECT_EQ(newHorizonsLighthouse::departureTown(*hero, other), nullptr);
	ASSERT_TRUE(handler->moveHero(hero->id, hero->convertFromVisitablePos(other),
		EMovementMode::STANDARD, false, hero->getOwner(), EPathfindingLayer::SAIL));
	EXPECT_EQ(hero->movementPointsRemaining(), 0);
	EXPECT_FALSE(newHorizonsLighthouse::hasDepartureBonus(*hero));
}

TEST_F(NewHorizonsLighthouseDepartureTest, DayExpiryAndOrdinaryDisembarkPenaltyRemain)
{
	ASSERT_NO_FATAL_FAILURE(start());
	ASSERT_TRUE(handler->moveHero(hero->id, hero->convertFromVisitablePos(port),
		EMovementMode::STANDARD, false, hero->getOwner(), EPathfindingLayer::SAIL));
	ASSERT_TRUE(newHorizonsLighthouse::hasDepartureBonus(*hero));
	EXPECT_FALSE(newHorizonsLighthouse::hasDepartureBonus(*hero, 1));
	ASSERT_TRUE(handler->moveHero(hero->id, hero->convertFromVisitablePos(shore),
		EMovementMode::STANDARD, false, hero->getOwner(), EPathfindingLayer::LAND));
	EXPECT_FALSE(hero->inBoat());
	EXPECT_EQ(hero->movementPointsRemaining(), 0);
	NewTurn next;
	next.day = gameState()->getCalendar().getCurrentDay() + 1;
	gameState()->apply(next);
	EXPECT_FALSE(newHorizonsLighthouse::hasDepartureBonus(*hero));
}

TEST_F(NewHorizonsLighthouseDepartureTest, InvalidDaySourceAndMovementRejectBeforeAnyStateMutation)
{
	ASSERT_NO_FATAL_FAILURE(start());
	const auto movement = hero->movementPointsRemaining();
	for(int invalid = 0; invalid != 3; ++invalid)
	{
		auto pack = receipt();
		if(invalid == 0) ++pack.lighthouseDeparture->day;
		if(invalid == 1) pack.lighthouseDeparture->town = hero->id;
		if(invalid == 2) ++pack.movePoints;
		EXPECT_THROW(gameState()->apply(pack), std::runtime_error);
		EXPECT_EQ(hero->visitablePos(), shore);
		EXPECT_EQ(hero->movementPointsRemaining(), movement);
		EXPECT_FALSE(hero->inBoat());
		EXPECT_FALSE(newHorizonsLighthouse::hasDepartureBonus(*hero));
	}
	auto valid = receipt();
	ASSERT_NO_THROW(gameState()->apply(valid));
	EXPECT_THROW(gameState()->apply(valid), std::runtime_error);
}

TEST_F(NewHorizonsLighthouseDepartureTest, CurrentReceiptRoundTripsAndOldWriterRejectsBeforePrefix)
{
	ASSERT_NO_FATAL_FAILURE(start());
	auto pack = receipt();
	CMemorySerializer memory;
	memory.oser & pack;
	TryMoveHero restored;
	memory.iser & restored;
	ASSERT_TRUE(restored.lighthouseDeparture);
	EXPECT_EQ(restored.lighthouseDeparture->town, town->id);
	EXPECT_EQ(restored.lighthouseDeparture->day, pack.lighthouseDeparture->day);
	EXPECT_EQ(restored.movePoints, pack.movePoints);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_CORONIUS_HOLY_WRATH;
	EXPECT_THROW(old.oser & pack, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	pack.lighthouseDeparture.reset();
	CMemorySerializer plain;
	plain.oser.version = ESerializationVersion::NEW_HORIZONS_CORONIUS_HOLY_WRATH;
	ASSERT_NO_THROW(plain.oser & pack);
	plain.iser.version = plain.oser.version;
	restored.lighthouseDeparture = newHorizonsLighthouse::DepartureReceipt{town->id, 1};
	ASSERT_NO_THROW(plain.iser & restored);
	EXPECT_FALSE(restored.lighthouseDeparture);
}

TEST_F(NewHorizonsLighthouseDepartureTest, CapturedRulesRejectOldMapWorldAndHeroBeforePrefix)
{
	ASSERT_NO_FATAL_FAILURE(start());
	LobbyStartGame lobby;
	lobby.initializedStartInfo = std::make_shared<StartInfo>(*gameState()->getStartInfo());
	lobby.initializedGameState = gameState();
	for(int kind = 0; kind != 4; ++kind)
	{
		CMemorySerializer memory;
		memory.oser.version = ESerializationVersion::NEW_HORIZONS_CORONIUS_HOLY_WRATH;
		if(kind == 0) EXPECT_THROW(memory.oser & *hero, std::runtime_error);
		if(kind == 1) EXPECT_THROW(memory.oser & *map(), std::runtime_error);
		if(kind == 2) EXPECT_THROW(memory.oser & *gameState(), std::runtime_error);
		if(kind == 3) EXPECT_THROW(memory.oser & lobby, std::runtime_error);
		EXPECT_TRUE(memory.extractBuffer().empty());
	}
}

TEST_F(NewHorizonsLighthouseDepartureTest, RepeatedSameDayDepartureDoesNotStackAndEnemyPortHasNoWaiver)
{
	ASSERT_NO_FATAL_FAILURE(start());
	ASSERT_TRUE(handler->moveHero(hero->id, hero->convertFromVisitablePos(port),
		EMovementMode::STANDARD, false, hero->getOwner(), EPathfindingLayer::SAIL));
	const auto seaMaximum = hero->getTurnInfo(0)->getMaxMovePoints(EPathfindingLayer::SAIL);
	ASSERT_TRUE(handler->moveHero(hero->id, hero->convertFromVisitablePos(shore),
		EMovementMode::STANDARD, false, hero->getOwner(), EPathfindingLayer::LAND));
	hero->setMovementPoints(hero->getTurnInfo(0)->getMaxMovePoints(EPathfindingLayer::LAND));
	ASSERT_TRUE(handler->moveHero(hero->id, hero->convertFromVisitablePos(port),
		EMovementMode::STANDARD, false, hero->getOwner(), EPathfindingLayer::SAIL));
	EXPECT_EQ(hero->getTurnInfo(0)->getMaxMovePoints(EPathfindingLayer::SAIL), seaMaximum);
	const auto bonuses = hero->getBonuses(Selector::type()(BonusType::MOVEMENT));
	EXPECT_EQ(std::count_if(bonuses->begin(), bonuses->end(), [](const auto & bonus)
		{ return bonus->stacking == newHorizonsLighthouse::STACKING_KEY; }), 1);
	ASSERT_TRUE(handler->moveHero(hero->id, hero->convertFromVisitablePos(shore),
		EMovementMode::STANDARD, false, hero->getOwner(), EPathfindingLayer::LAND));
	handler->setOwner(town, PlayerColor::NEUTRAL);
	EXPECT_EQ(newHorizonsLighthouse::departureTown(*hero, port), nullptr);
}

TEST_F(NewHorizonsLighthouseDepartureTest, OneDayBonusAndCapturedRulesRoundTripWithoutInventingFutureDays)
{
	ASSERT_NO_FATAL_FAILURE(start());
	auto bonus = newHorizonsLighthouse::departureBonus(*town, *hero);
	JsonNode rules = hero->getPrimaryGrowthRules();
	CMemorySerializer memory;
	memory.oser & bonus;
	memory.oser & rules;
	Bonus restored;
	JsonNode restoredRules;
	memory.iser & restored;
	memory.iser & restoredRules;
	EXPECT_EQ(restored.stacking, newHorizonsLighthouse::STACKING_KEY);
	EXPECT_EQ(restored.duration, BonusDuration::ONE_DAY);
	EXPECT_EQ(restored.val, 20);
	EXPECT_EQ(restored.valType, BonusValueType::PERCENT_TO_BASE);
	EXPECT_EQ(restored.sid, bonus.sid);
	EXPECT_EQ(restoredRules, rules);
	EXPECT_NO_THROW(newHorizonsLighthouse::validateRulesSerialization(restoredRules, true));
}

TEST(NewHorizonsLighthouseRulesTest, MalformedPresenceRejectsAndAbsentSnapshotRemainsPortable)
{
	JsonNode rules;
	EXPECT_NO_THROW(newHorizonsLighthouse::validateRulesSerialization(rules, false));
	rules["lighthouseDeparture"]["enabled"].Bool() = true;
	rules["lighthouseDeparture"]["seaMovementPercent"].Float() = 20;
	EXPECT_NO_THROW(newHorizonsLighthouse::validateRulesSerialization(rules, true));
	EXPECT_THROW(newHorizonsLighthouse::validateRulesSerialization(rules, false), std::runtime_error);
	rules["lighthouseDeparture"]["seaMovementPercent"].Float() = 20.5;
	EXPECT_THROW(newHorizonsLighthouse::validateRulesSerialization(rules, true), std::runtime_error);
}
