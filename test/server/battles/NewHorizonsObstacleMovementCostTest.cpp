/*
 * NewHorizonsObstacleMovementCostTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of the license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/battle/ReachabilityInfo.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/JsonDeserializer.h"
#include "../../../lib/serializer/JsonSerializer.h"
#include "../../../server/CGameHandler.h"

#include <initializer_list>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

class NewHorizonsObstacleMovementCostTest : public HeroCommandFixture
{
protected:
	void prepareMover(const std::string & creature, const BattleHex & position)
	{
		ASSERT_NO_FATAL_FAILURE(startGame());
		ASSERT_NO_FATAL_FAILURE(startBattle());
		mover = addStack(BattleSide::ATTACKER, creatureByName(creature), position, 1);
		ASSERT_NE(mover, nullptr);
		ASSERT_NO_FATAL_FAILURE(beginCombat());
	}

	void addMovementField(uint32_t uniqueId, int32_t movementCost, int32_t turnsRemaining,
		std::initializer_list<BattleHex> tiles)
	{
		ASSERT_FALSE(tiles.size() == 0);

		SpellCreatedObstacle obstacle;
		obstacle.uniqueID = static_cast<si32>(uniqueId);
		obstacle.ID = SpellID::EARTHQUAKE;
		obstacle.trigger = SpellID::NONE;
		obstacle.pos = *tiles.begin();
		obstacle.casterSide = BattleSide::ATTACKER;
		obstacle.turnsRemaining = turnsRemaining;
		obstacle.movementCost = movementCost;
		obstacle.passable = true;
		for(const auto & hex : tiles)
			obstacle.customSize.insert(hex);

		BattleObstaclesChanged packet;
		packet.battleID = BattleID(0);
		packet.change = ObstacleChanges(uniqueId, BattleChanges::EOperation::ADD);
		obstacle.toInfo(packet.change);
		gameHandler->sendAndApply(packet);
	}

	ReachabilityInfo reachableFrom(const BattleHex & position) const
	{
		return battle()->getReachability(ReachabilityInfo::Parameters(mover, position));
	}

	CStack * mover = nullptr;
};

TEST_F(NewHorizonsObstacleMovementCostTest, ChargesOnlyWhenEnteringAnAffectedHex)
{
	const BattleHex start(4, 5);
	const BattleHex field(5, 5);
	const BattleHex outside(6, 5);
	ASSERT_NO_FATAL_FAILURE(prepareMover("core:pikeman", start));
	addMovementField(100, 1, 3, {field});

	const auto fromOutside = reachableFrom(start);
	ASSERT_TRUE(fromOutside.isReachable(field));
	EXPECT_EQ(fromOutside.distances[field.toInt()], 2u)
		<< "Entering the field pays one ordinary step plus the one-point surcharge";

	const auto startingInField = reachableFrom(field);
	EXPECT_EQ(startingInField.distances[field.toInt()], 0u);
	ASSERT_TRUE(startingInField.isReachable(outside));
	EXPECT_EQ(startingInField.distances[outside.toInt()], 1u)
		<< "Starting in or leaving the field does not charge for the occupied hex";
}

TEST_F(NewHorizonsObstacleMovementCostTest, WeightedSearchChoosesTheCheaperDetour)
{
	const BattleHex start(4, 5);
	const BattleHex expensiveHex(6, 5);
	const BattleHex destination(8, 5);
	ASSERT_NO_FATAL_FAILURE(prepareMover("core:pikeman", start));

	const auto openField = reachableFrom(start);
	ASSERT_EQ(openField.distances[destination.toInt()], 4u);

	addMovementField(101, 3, 3, {expensiveHex});
	const auto weighted = reachableFrom(start);
	ASSERT_TRUE(weighted.isReachable(destination));
	EXPECT_EQ(weighted.distances[destination.toInt()], 5u)
		<< "The direct four-step route costs seven; the open five-step detour is cheaper";

	BattleHex cursor = destination;
	bool routeEntersField = cursor == expensiveHex;
	int routeSteps = 0;
	while(cursor != start && routeSteps < GameConstants::BFIELD_SIZE)
	{
		cursor = weighted.predecessors[cursor.toInt()];
		ASSERT_TRUE(cursor.isValid());
		routeEntersField = routeEntersField || cursor == expensiveHex;
		++routeSteps;
	}
	ASSERT_EQ(cursor, start);
	EXPECT_EQ(routeSteps, 5);
	EXPECT_FALSE(routeEntersField);
}

TEST_F(NewHorizonsObstacleMovementCostTest, DoubleWideFootprintPaysForItsNewTailHex)
{
	const BattleHex start(4, 5);
	const BattleHex destination(4, 4);
	ASSERT_NO_FATAL_FAILURE(prepareMover("core:hydra", start));
	ASSERT_TRUE(mover->doubleWide());
	ASSERT_FALSE(mover->hasBonusOfType(BonusType::FLYING));
	const auto currentFootprint = mover->getHexes();
	const auto destinationFootprint = mover->getHexes(destination);
	const BattleHex newlyOccupiedTail = mover->occupiedHex(destination);
	ASSERT_TRUE(destinationFootprint.contains(newlyOccupiedTail));
	ASSERT_FALSE(currentFootprint.contains(newlyOccupiedTail));
	addMovementField(102, 2, 3, {newlyOccupiedTail});

	const auto reachability = reachableFrom(start);
	ASSERT_TRUE(reachability.isReachable(destination));
	EXPECT_EQ(reachability.distances[destination.toInt()], 3u)
		<< "The new tail hex is charged once even though the head hex is outside the field";
}

TEST_F(NewHorizonsObstacleMovementCostTest, FlyingMovementIgnoresTheSurcharge)
{
	const BattleHex start(4, 5);
	const BattleHex destination(5, 5);
	ASSERT_NO_FATAL_FAILURE(prepareMover("core:pikeman", start));
	mover->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::FLYING,
		BonusSource::OTHER, 0, BonusSourceID()));
	addMovementField(103, 5, 3, {destination});

	const ReachabilityInfo::Parameters parameters(mover, start);
	ASSERT_TRUE(parameters.flying);
	const auto reachability = battle()->getReachability(parameters);
	ASSERT_TRUE(reachability.isReachable(destination));
	EXPECT_EQ(reachability.distances[destination.toInt()], 1u);
}

TEST_F(NewHorizonsObstacleMovementCostTest, OverlappingFieldsUseTheMaximumCost)
{
	const BattleHex start(4, 5);
	const BattleHex field(5, 5);
	ASSERT_NO_FATAL_FAILURE(prepareMover("core:pikeman", start));
	addMovementField(104, 1, 3, {field});
	addMovementField(105, 4, 3, {field});

	ASSERT_EQ(battle()->obstacles.size(), 2u);
	const auto reachability = reachableFrom(start);
	ASSERT_TRUE(reachability.isReachable(field));
	EXPECT_EQ(reachability.distances[field.toInt()], 5u)
		<< "The four-point field dominates the one-point field instead of stacking with it";
}

TEST_F(NewHorizonsObstacleMovementCostTest, ExpiredFieldNoLongerAddsMovementCost)
{
	const BattleHex start(4, 5);
	const BattleHex field(5, 5);
	ASSERT_NO_FATAL_FAILURE(prepareMover("core:pikeman", start));
	addMovementField(106, 1, 1, {field});

	ASSERT_EQ(reachableFrom(start).distances[field.toInt()], 2u);
	ASSERT_NO_FATAL_FAILURE(endRound());

	EXPECT_TRUE(battle()->obstacles.empty());
	EXPECT_EQ(reachableFrom(start).distances[field.toInt()], 1u);
}

TEST_F(NewHorizonsObstacleMovementCostTest, MovementCostPersistsInJsonPacketsAndVersionedBinaryState)
{
	SpellCreatedObstacle obstacle;
	obstacle.uniqueID = 107;
	obstacle.ID = SpellID::EARTHQUAKE;
	obstacle.trigger = SpellID::NONE;
	obstacle.pos = BattleHex(5, 5);
	obstacle.turnsRemaining = 3;
	obstacle.passable = true;
	obstacle.movementCost = 7;
	obstacle.customSize.insert(BattleHex(5, 5));

	JsonNode json;
	JsonSerializer jsonWriter(nullptr, json);
	obstacle.serializeJson(jsonWriter);
	ASSERT_TRUE(json["movementCost"].isNumber());
	EXPECT_EQ(json["movementCost"].Integer(), 7);

	SpellCreatedObstacle jsonRestored;
	JsonDeserializer jsonReader(nullptr, json);
	jsonRestored.serializeJson(jsonReader);
	EXPECT_EQ(jsonRestored.movementCost, 7);

	JsonNode legacyJson = json;
	legacyJson.Struct().erase("movementCost");
	SpellCreatedObstacle legacyJsonRestored;
	JsonDeserializer legacyJsonReader(nullptr, legacyJson);
	legacyJsonRestored.serializeJson(legacyJsonReader);
	EXPECT_EQ(legacyJsonRestored.movementCost, 0);

	for(const int32_t invalidCost : {-1, SpellCreatedObstacle::MAX_MOVEMENT_COST + 1})
	{
		JsonNode invalidInput = json;
		invalidInput["movementCost"] = JsonNode(invalidCost);
		SpellCreatedObstacle invalidRestored;
		JsonDeserializer invalidReader(nullptr, invalidInput);
		EXPECT_THROW(invalidRestored.serializeJson(invalidReader), std::runtime_error);

		SpellCreatedObstacle invalidObstacle = obstacle;
		invalidObstacle.movementCost = invalidCost;
		JsonNode invalidJson;
		JsonSerializer invalidWriter(nullptr, invalidJson);
		EXPECT_THROW(invalidObstacle.serializeJson(invalidWriter), std::runtime_error);
	}

	ObstacleChanges packetChange(obstacle.uniqueID, BattleChanges::EOperation::ADD);
	obstacle.toInfo(packetChange);
	ASSERT_TRUE(packetChange.data["obstacle"]["movementCost"].isNumber());
	EXPECT_EQ(packetChange.data["obstacle"]["movementCost"].Integer(), 7);
	SpellCreatedObstacle packetRestored;
	packetRestored.fromInfo(packetChange);
	EXPECT_EQ(packetRestored.movementCost, 7);

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & obstacle);
	SpellCreatedObstacle binaryRestored;
	ASSERT_NO_THROW(current.iser & binaryRestored);
	EXPECT_EQ(binaryRestored.movementCost, 7);

	CMemorySerializer oldWriter;
	oldWriter.oser.version = ESerializationVersion::NEW_HORIZONS_LAND_SURVEYOR;
	EXPECT_THROW(oldWriter.oser & obstacle, std::runtime_error);
	EXPECT_TRUE(oldWriter.extractBuffer().empty());

	SpellCreatedObstacle legacyObstacle;
	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::NEW_HORIZONS_LAND_SURVEYOR;
	legacy.iser.version = ESerializationVersion::NEW_HORIZONS_LAND_SURVEYOR;
	ASSERT_NO_THROW(legacy.oser & legacyObstacle);
	SpellCreatedObstacle legacyRestored;
	ASSERT_NO_THROW(legacy.iser & legacyRestored);
	EXPECT_EQ(legacyRestored.movementCost, 0);
}
