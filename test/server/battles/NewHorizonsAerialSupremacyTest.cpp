/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsAerialSupremacyFixture.h"

TEST_F(NewHorizonsAerialSupremacyBattleTest, OrdinaryCounterKeepsOriginAndUsesOneExistingRetaliation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(newHorizonsHeroes::aerialSupremacyEnabled(defenderSideHero));
	const auto origin = griffin->getPosition();
	const auto counters = griffin->counterAttacks.available();
	const auto excursion = newHorizonsAerialSupremacy::prepare(*battle(), griffin, shooter);
	ASSERT_TRUE(excursion);
	EXPECT_NE(excursion->landing, origin);
	ASSERT_TRUE(shoot());
	EXPECT_EQ(griffin->getPosition(), origin);
	EXPECT_EQ(griffin->counterAttacks.available(), counters - 1);
	EXPECT_EQ(griffin->aerialRetaliationRound, battle()->getRound());
	ASSERT_EQ(server.attacks.size(), 2);
	EXPECT_TRUE(server.attacks.back().counter());
	EXPECT_FALSE(server.attacks.back().shot());
	ASSERT_TRUE(server.attacks.back().aerialExcursion);
	EXPECT_EQ(server.attacks.back().aerialExcursion->origin, origin);
	EXPECT_FALSE(newHorizonsAerialSupremacy::prepare(*battle(), griffin, shooter));
}
TEST_F(NewHorizonsAerialSupremacyBattleTest, HistoricalAbsenceKeepsOldSpecialtyAndNoExcursion)
{
	captured = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(newHorizonsHeroes::hasAerialSupremacy(defenderSideHero));
	EXPECT_FALSE(newHorizonsAerialSupremacy::prepare(*battle(), griffin, shooter));
	ASSERT_TRUE(shoot());
	ASSERT_EQ(server.attacks.size(), 1);
	EXPECT_EQ(griffin->aerialRetaliationRound, -1);
}
TEST_F(NewHorizonsAerialSupremacyBattleTest, RoyalIgnoresRegularAerialRoundLimit)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	griffin->aerialRetaliationRound = battle()->getRound();
	EXPECT_TRUE(newHorizonsAerialSupremacy::prepare(*battle(), griffin, shooter));
	EXPECT_TRUE(griffin->counterAttacks.canUse());
}
TEST_F(NewHorizonsAerialSupremacyBattleTest, RangeBlockerAndStaleGeometryRejectWithoutMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto planned = newHorizonsAerialSupremacy::prepare(*battle(), griffin, shooter);
	ASSERT_TRUE(planned);
	const auto before = griffin->save();
	battle()->moveUnit(shooter->unitId(), BattleHex(1, 1));
	EXPECT_FALSE(newHorizonsAerialSupremacy::matches(*battle(), *planned));
	EXPECT_THROW(newHorizonsAerialSupremacy::validate(*battle(), *planned), std::runtime_error);
	EXPECT_EQ(griffin->save(), before);
}
TEST(NewHorizonsAerialSupremacyShapeTest, RejectsMalformedRoundAndIdentity)
{
	newHorizonsAerialSupremacy::Excursion valid{1, 2, BattleHex(10, 5), BattleHex(6, 5), BattleHex(5, 5), 1};
	EXPECT_NO_THROW(valid.validateShape());
	valid.opponent = valid.actor;
	EXPECT_THROW(valid.validateShape(), std::runtime_error);
	JsonNode snapshot;
	snapshot["state"]["aerialRetaliationRound"].Integer() = -2;
	EXPECT_THROW(newHorizonsAerialSupremacy::hasRoundState(snapshot), std::runtime_error);
	snapshot["state"].Struct().erase("aerialRetaliationRound");
	EXPECT_FALSE(newHorizonsAerialSupremacy::hasRoundState(snapshot));
}
TEST_F(NewHorizonsAerialSupremacyBattleTest, FirstStrikeExecutesOnePhysicalCounterBeforeOpeningShot)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	griffin->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::FIRST_STRIKE, BonusSource::OTHER, 1, BonusSourceID()));
	const auto origin = griffin->getPosition();
	ASSERT_TRUE(shoot());
	ASSERT_EQ(server.attacks.size(), 1);
	EXPECT_TRUE(server.attacks.front().counter());
	EXPECT_FALSE(server.attacks.front().shot());
	EXPECT_FALSE(shooter->alive());
	EXPECT_EQ(griffin->getPosition(), origin);
}
TEST_F(NewHorizonsAerialSupremacyBattleTest, RegularAerialLimitIsCopiedAndExpiresByRoundComparisonOnly)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(griffin->counterAttacks.total(), 2);
	griffin->aerialRetaliationRound = battle()->getRound();
	const auto copy = griffin->acquireState();
	EXPECT_EQ(copy->aerialRetaliationRound, griffin->aerialRetaliationRound);
	EXPECT_FALSE(newHorizonsAerialSupremacy::prepare(*battle(), griffin, shooter));
	EXPECT_EQ(griffin->counterAttacks.available(), 2);
	griffin->aerialRetaliationRound = battle()->getRound() - 1;
	EXPECT_TRUE(newHorizonsAerialSupremacy::prepare(*battle(), griffin, shooter));
	EXPECT_EQ(copy->aerialRetaliationRound, battle()->getRound());
}
TEST_F(NewHorizonsAerialSupremacyBattleTest, IncomingRangedRetaliationBlockPreservesOrdinaryResources)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::BLOCKS_RANGED_RETALIATION, BonusSource::OTHER, 1, BonusSourceID()));
	const auto before = griffin->counterAttacks.available();
	EXPECT_FALSE(newHorizonsAerialSupremacy::prepare(*battle(), griffin, shooter));
	ASSERT_TRUE(shoot());
	EXPECT_EQ(server.attacks.size(), 1);
	EXPECT_EQ(griffin->counterAttacks.available(), before);
	EXPECT_EQ(griffin->aerialRetaliationRound, -1);
}

TEST_F(NewHorizonsAerialSupremacyBattleTest, StackDescriptorRoundtripPreservesMarkerAndOldReadClearsReusedState)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	griffin->aerialRetaliationRound = battle()->getRound();
	CMemorySerializer current;
	current.iser.cb = gameState().get();
	current.oser & *griffin;
	CStack loaded;
	current.iser & loaded;
	EXPECT_EQ(loaded.aerialRetaliationRound, griffin->aerialRetaliationRound);
	griffin->aerialRetaliationRound = -1;
	CMemorySerializer historical;
	historical.oser.version = historical.iser.version = ESerializationVersion::NEW_HORIZONS_PARALYZING_GAZE;
	historical.iser.cb = gameState().get();
	historical.oser & *griffin;
	historical.iser & loaded;
	EXPECT_EQ(loaded.aerialRetaliationRound, -1);
}
TEST_F(NewHorizonsAerialSupremacyBattleTest, ShooterFootprintMustBeWithinRangeEvenWhenAdjacentLandingIsReachable)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto range = griffin->getMovementRange(0);
	ASSERT_GT(range, 0);
	ASSERT_LT(range, 9);
	const BattleHex beyond(10 - range - 1, 5);
	battle()->moveUnit(shooter->unitId(), beyond);
	uint32_t footprintDistance = ReachabilityInfo::INFINITE_DIST;
	for(const auto & actorHex : griffin->getHexes())
		for(const auto & targetHex : shooter->getHexes())
			footprintDistance = std::min<uint32_t>(footprintDistance, BattleHex::getDistance(actorHex, targetHex));
	ASSERT_EQ(footprintDistance, range + 1);
	const auto reach = battle()->getReachability(griffin);
	const BattleHex adjacent(10 - range, 5);
	ASSERT_LE(reach.distances[adjacent.toInt()], range);
	ASSERT_TRUE(reach.accessibility.accessibleForMovementEndpoint(adjacent, griffin->doubleWide(), griffin->unitSide()));
	EXPECT_FALSE(newHorizonsAerialSupremacy::prepare(*battle(), griffin, shooter));
	EXPECT_EQ(griffin->aerialRetaliationRound, -1);
}
