/*
 * NewHorizonsMarchLeftTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "ArmyAdmissionTestFixture.h"

#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../lib/networkPacks/StackLocation.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/gameState/TavernSlot.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/pathfinder/NewHorizonsMarchLeft.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../server/CGameHandler.h"
#include "../mock/TinyH3MBuilder.h"
#include "../OriginalDataRequirement.h"

namespace dm = newHorizonsMarchLeft; // Day's March

TEST(NewHorizonsMarchLeftRule, SpecExampleLimitsTheReceiverToHisShareOfTheDay)
{
	// A 2000-point hero who used 1500 hands his troops to a fresh 1800-point hero.
	const int left = dm::marchLeft(500, 2000);
	EXPECT_EQ(left, 2500);
	EXPECT_EQ(dm::percentLeft(left), 25);
	EXPECT_EQ(dm::movementAfterReceiving(1800, 1800, left), 450);
	// The relay gains nothing over the 500 the first hero had left.
	EXPECT_LT(dm::movementAfterReceiving(1800, 1800, left), 500);
}

TEST(NewHorizonsMarchLeftRule, ShareIsClampedAndTheLimitNeverRaisesMovement)
{
	EXPECT_EQ(dm::marchLeft(2000, 2000), dm::FULL);
	EXPECT_EQ(dm::marchLeft(2400, 2000), dm::FULL); // Stables or carried movement count as unused
	EXPECT_EQ(dm::marchLeft(0, 2000), 0);
	EXPECT_EQ(dm::marchLeft(-5, 2000), 0);
	EXPECT_EQ(dm::marchLeft(100, 0), dm::FULL);
	EXPECT_EQ(dm::marchLeft(1999, 2000), 9995); // rounded down, never hides a step
	EXPECT_EQ(dm::movementAfterReceiving(300, 1800, 2500), 300);
	EXPECT_EQ(dm::movementAfterReceiving(2400, 2000, dm::FULL), 2400);
	EXPECT_EQ(dm::movementAfterReceiving(1800, 1800, 0), 0);
	EXPECT_EQ(dm::movementAfterReceiving(1800, 1800, -dm::FULL), 0);
	EXPECT_EQ(dm::percentLeft(0), 0);
	EXPECT_EQ(dm::percentLeft(dm::FULL), 100);
	EXPECT_EQ(dm::percentLeft(49), 0);
	EXPECT_EQ(dm::percentLeft(50), 1);
}

TEST(NewHorizonsMarchLeftRule, MarchLeftBelongsToItsDayMergesLowAndSplitsByCopy)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA(); // Pikemen are resolved through the loaded creatures.
	CStackInstance marched(nullptr, CreatureID(CreatureID::decode("core:pikeman")), 10);
	CStackInstance fresh(nullptr, CreatureID(CreatureID::decode("core:pikeman")), 10);
	marched.lowerMarchLeft(4, 4000);
	marched.lowerMarchLeft(4, 7000); // never rises during the day
	EXPECT_EQ(marched.getMarchLeft(4), 4000);
	EXPECT_EQ(marched.getMarchLeft(5), dm::FULL); // a new day starts full
	EXPECT_EQ(fresh.getMarchLeft(4), dm::FULL);

	fresh.mergeMarchLeft(marched);
	EXPECT_EQ(fresh.getMarchLeft(4), 4000);
	CStackInstance older(nullptr, CreatureID(CreatureID::decode("core:pikeman")), 10);
	older.lowerMarchLeft(3, 1000);
	fresh.mergeMarchLeft(older); // yesterday's march does not limit today's stack
	EXPECT_EQ(fresh.getMarchLeft(4), 4000);
	older.mergeMarchLeft(marched); // today's march replaces yesterday's record
	EXPECT_EQ(older.getMarchLeft(4), 4000);
	EXPECT_EQ(older.getMarchLeft(3), dm::FULL);

	CStackInstance split(nullptr, CreatureID(CreatureID::decode("core:pikeman")), 1);
	split.copyMarchLeft(marched);
	EXPECT_EQ(split.getMarchLeft(4), 4000);
	marched.lowerMarchLeft(5, 9000);
	EXPECT_EQ(marched.getMarchLeft(5), 9000);
	EXPECT_EQ(marched.getMarchLeft(4), dm::FULL);
}

TEST(NewHorizonsMarchLeftRule, MovementLimitFlagRoundTripsAndOlderPacketsDropIt)
{
	SetMovePoints limit(ObjectInstanceID(3), 450);
	limit.marchLeftLimit = true;
	CMemorySerializer current;
	current.oser & limit;
	SetMovePoints restored;
	current.iser & restored;
	EXPECT_TRUE(restored.marchLeftLimit);
	EXPECT_EQ(restored.val, 450);

	// 1168 (Warcasting) is the last format before Day's March; 1167 stays covered too.
	for(const auto version : {ESerializationVersion::NEW_HORIZONS_WARCAST, ESerializationVersion::NEW_HORIZONS_SYLVAN_AMBUSH})
	{
		CMemorySerializer older;
		older.oser.version = version;
		older.iser.version = version;
		older.oser & limit;
		SetMovePoints legacy;
		legacy.marchLeftLimit = true;
		older.iser & legacy;
		EXPECT_FALSE(legacy.marchLeftLimit) << static_cast<int>(version);
		EXPECT_EQ(legacy.val, 450) << static_cast<int>(version);
	}

	SetMovePoints malformed(ObjectInstanceID(3), -1);
	malformed.marchLeftLimit = true;
	EXPECT_THROW(malformed.validatePursuitMarchShape(), std::runtime_error);
}

class NewHorizonsMarchLeftTest : public TinyMapGameTest
{
protected:
	const PlayerColor player = PlayerColor(0);
	const CreatureID pikeman = CreatureID(CreatureID::decode("core:pikeman"));
	const CreatureID archer = CreatureID(CreatureID::decode("core:archer"));
	CGHeroInstance * giver = nullptr;
	CGHeroInstance * receiver = nullptr;
	CGHeroInstance * third = nullptr;
	CGTownInstance * town = nullptr;
	std::unique_ptr<armyAdmissionTest::LeadershipRecordingServer> server;
	std::unique_ptr<CGameHandler> handler;

	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
	}

	void start()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		// A second player keeps the first one from winning after his first exchange.
		builder.size(36, false).playerActive(player).playerActive(PlayerColor(1))
			.town({12, 12, 0}, FactionID::CASTLE, player).townGarrison({{archer, 6}})
			.town({30, 30, 0}, FactionID::CASTLE, PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), player)
			.hero({9, 5, 0}, HeroTypeID(HeroTypeID::decode("core:valeska")), player)
			.hero({13, 5, 0}, HeroTypeID(HeroTypeID::decode("core:sorsha")), player);
		startWithMap(std::move(builder));
		giver = findHeroAt({5, 5, 0});
		receiver = findHeroAt({9, 5, 0});
		third = findHeroAt({13, 5, 0});
		town = findFirst<CGTownInstance>();
		ASSERT_NE(giver, nullptr);
		ASSERT_NE(receiver, nullptr);
		ASSERT_NE(third, nullptr);
		third->clearSlots();
		ASSERT_TRUE(third->setCreature(SlotID(0), archer, 2));
		setMovement(*third, dm::fullDailyMovement(*third));
		ASSERT_NE(town, nullptr);
		ASSERT_TRUE(dm::appliesTo(*giver));
		ASSERT_TRUE(dm::appliesTo(*receiver));
		town->clearSlots();
		giver->clearSlots();
		receiver->clearSlots();
		ASSERT_TRUE(giver->setCreature(SlotID(0), pikeman, 20));
		ASSERT_TRUE(giver->setCreature(SlotID(1), archer, 5));
		ASSERT_TRUE(receiver->setCreature(SlotID(0), pikeman, 3));
		setMovement(*giver, dm::fullDailyMovement(*giver));
		setMovement(*receiver, dm::fullDailyMovement(*receiver));
		server = std::make_unique<armyAdmissionTest::LeadershipRecordingServer>(gameState());
		handler = std::make_unique<CGameHandler>(*server, gameState());
		gameState()->actingPlayers.insert(player);
	}

	int day() const { return gameState()->getCalendar().getCurrentDay(); }

	/// Sets movement through an ordinary movement packet (records a march when it lowers movement).
	void setMovement(CGHeroInstance & hero, int value)
	{
		SetMovePoints refresh(hero.id, value);
		gameState()->apply(refresh);
	}

	/// Spends movement the way Town Portal, Dimension Door or a visit records it.
	void march(CGHeroInstance & hero, int remaining)
	{
		SetMovePoints spend(hero.id, remaining);
		handler->sendAndApply(spend);
	}

	void arrange(ui8 what, SlotID first, SlotID second, ObjectInstanceID firstArmy, ObjectInstanceID secondArmy, int value = 0)
	{
		ArrangeStacks request(what, first, second, firstArmy, secondArmy, value);
		request.player = player;
		request.requestID = ++requests;
		handler->handleReceivedPack(GameConnectionID::FIRST_CONNECTION, request);
		ASSERT_FALSE(server->responses.empty());
		ASSERT_TRUE(server->responses.back().result);
	}

	/// Opens the authoritative hero exchange between the two heroes.
	void meet()
	{
		handler->heroExchange(giver->id, receiver->id);
	}

	int requests = 0;
};

TEST_F(NewHorizonsMarchLeftTest, SpendingMovementLowersEveryStacksMarchLeft)
{
	ASSERT_NO_FATAL_FAILURE(start());
	const int full = dm::fullDailyMovement(*giver);
	march(*giver, full / 4);
	const int share = dm::marchLeft(full / 4, full);
	EXPECT_LE(share, 2500);
	EXPECT_EQ(giver->getStack(SlotID(0)).getMarchLeft(day()), share);
	EXPECT_EQ(giver->getStack(SlotID(1)).getMarchLeft(day()), share);
	// March left never rises during the day, even when movement is restored.
	march(*giver, full);
	EXPECT_EQ(giver->getStack(SlotID(0)).getMarchLeft(day()), share);
	EXPECT_EQ(receiver->getStack(SlotID(0)).getMarchLeft(day()), dm::FULL);
}

TEST_F(NewHorizonsMarchLeftTest, WalkingRecordsTheMarchThroughTheAuthoritativeMove)
{
	ASSERT_NO_FATAL_FAILURE(start());
	const int before = giver->movementPointsRemaining();
	const auto destination = giver->visitablePos() + int3(1, 0, 0);
	ASSERT_TRUE(handler->moveHero(giver->id, giver->convertFromVisitablePos(destination),
		EMovementMode::STANDARD, false, player, EPathfindingLayer::LAND));
	ASSERT_LT(giver->movementPointsRemaining(), before);
	const int expected = dm::marchLeft(giver->movementPointsRemaining(), dm::fullDailyMovement(*giver));
	EXPECT_GT(expected, 0);
	EXPECT_EQ(giver->getStack(SlotID(0)).getMarchLeft(day()), expected);
}

TEST_F(NewHorizonsMarchLeftTest, ReceivingTiredTroopsLimitsOnlyTheReceiver)
{
	ASSERT_NO_FATAL_FAILURE(start());
	meet();
	const int giverFull = dm::fullDailyMovement(*giver);
	march(*giver, giverFull / 4);
	const int share = giver->getStack(SlotID(1)).getMarchLeft(day());
	const int giverLeft = giver->movementPointsRemaining();
	const int receiverFull = dm::fullDailyMovement(*receiver);
	ASSERT_EQ(receiver->movementPointsRemaining(), receiverFull);

	ASSERT_NO_FATAL_FAILURE(arrange(1, SlotID(1), SlotID(1), giver->id, receiver->id)); // archers into an empty slot
	ASSERT_TRUE(receiver->hasStackAtSlot(SlotID(1)));
	EXPECT_EQ(receiver->getStack(SlotID(1)).getMarchLeft(day()), share);
	EXPECT_EQ(receiver->movementPointsRemaining(), dm::movementAfterReceiving(receiverFull, receiverFull, share));
	EXPECT_LE(receiver->movementPointsRemaining(), receiverFull / 4);
	EXPECT_EQ(giver->movementPointsRemaining(), giverLeft);
	// The receiver's own stack did not march: the limit is not a march of its own.
	EXPECT_EQ(receiver->getStack(SlotID(0)).getMarchLeft(day()), dm::FULL);

	// Receiving again never raises the movement back.
	const int limited = receiver->movementPointsRemaining();
	ASSERT_NO_FATAL_FAILURE(arrange(1, SlotID(1), SlotID(1), receiver->id, giver->id));
	ASSERT_NO_FATAL_FAILURE(arrange(1, SlotID(1), SlotID(1), giver->id, receiver->id));
	EXPECT_EQ(receiver->movementPointsRemaining(), limited);
}

TEST_F(NewHorizonsMarchLeftTest, FreshTroopsAndAlreadyLowMovementAreUnaffected)
{
	ASSERT_NO_FATAL_FAILURE(start());
	meet();
	const int receiverFull = dm::fullDailyMovement(*receiver);
	ASSERT_NO_FATAL_FAILURE(arrange(1, SlotID(1), SlotID(1), giver->id, receiver->id)); // the giver has not marched
	EXPECT_EQ(receiver->movementPointsRemaining(), receiverFull);

	setMovement(*receiver, 10);
	march(*giver, dm::fullDailyMovement(*giver) / 2);
	ASSERT_NO_FATAL_FAILURE(arrange(1, SlotID(1), SlotID(1), receiver->id, giver->id));
	ASSERT_NO_FATAL_FAILURE(arrange(1, SlotID(1), SlotID(1), giver->id, receiver->id));
	EXPECT_EQ(receiver->movementPointsRemaining(), 10);
}

TEST_F(NewHorizonsMarchLeftTest, MergingKeepsTheLowerMarchLeftAndSplittingCopiesIt)
{
	ASSERT_NO_FATAL_FAILURE(start());
	meet();
	march(*giver, dm::fullDailyMovement(*giver) / 2);
	const int share = giver->getStack(SlotID(0)).getMarchLeft(day());
	ASSERT_LT(share, dm::FULL);
	// Split 5 of the giver's marched pikemen onto the receiver's fresh pikemen.
	ASSERT_NO_FATAL_FAILURE(arrange(3, SlotID(0), SlotID(0), giver->id, receiver->id, 8));
	EXPECT_EQ(receiver->getStackCount(SlotID(0)), 8);
	EXPECT_EQ(receiver->getStack(SlotID(0)).getMarchLeft(day()), share);
	EXPECT_EQ(giver->getStack(SlotID(0)).getMarchLeft(day()), share);
	const int receiverFull = dm::fullDailyMovement(*receiver);
	EXPECT_EQ(receiver->movementPointsRemaining(), dm::movementAfterReceiving(receiverFull, receiverFull, share));
	// A split inside one army copies the march left and limits nobody.
	const int limited = receiver->movementPointsRemaining();
	ASSERT_NO_FATAL_FAILURE(arrange(3, SlotID(0), SlotID(4), receiver->id, receiver->id, 2));
	ASSERT_TRUE(receiver->hasStackAtSlot(SlotID(4)));
	EXPECT_EQ(receiver->getStack(SlotID(4)).getMarchLeft(day()), share);
	EXPECT_EQ(receiver->movementPointsRemaining(), limited);
}

TEST_F(NewHorizonsMarchLeftTest, TroopsLeftInATownKeepTheirMarchLeftForTheNextHero)
{
	ASSERT_NO_FATAL_FAILURE(start());
	town->setVisitingHero(giver);
	march(*giver, dm::fullDailyMovement(*giver) / 4);
	const int share = giver->getStack(SlotID(1)).getMarchLeft(day());
	ASSERT_NO_FATAL_FAILURE(arrange(1, SlotID(1), SlotID(2), giver->id, town->id));
	ASSERT_TRUE(town->hasStackAtSlot(SlotID(2)));
	EXPECT_EQ(town->getStack(SlotID(2)).getMarchLeft(day()), share);

	// The giver walks away; a fresh hero visits the same town and takes the troops.
	town->setVisitingHero(nullptr);
	town->setVisitingHero(receiver);
	const int receiverFull = dm::fullDailyMovement(*receiver);
	ASSERT_NO_FATAL_FAILURE(arrange(1, SlotID(2), SlotID(3), town->id, receiver->id));
	ASSERT_TRUE(receiver->hasStackAtSlot(SlotID(3)));
	EXPECT_EQ(receiver->getStack(SlotID(3)).getMarchLeft(day()), share);
	EXPECT_EQ(receiver->movementPointsRemaining(), dm::movementAfterReceiving(receiverFull, receiverFull, share));
}

TEST_F(NewHorizonsMarchLeftTest, ANewDayGivesEveryStackAFullMarch)
{
	ASSERT_NO_FATAL_FAILURE(start());
	meet();
	march(*giver, 0);
	ASSERT_EQ(giver->getStack(SlotID(1)).getMarchLeft(day()), 0);
	NewTurn turn;
	turn.day = day() + 1;
	turn.heroesMovement.emplace_back(giver->id, dm::fullDailyMovement(*giver));
	gameState()->apply(turn);
	ASSERT_EQ(day(), turn.day);
	EXPECT_EQ(giver->getStack(SlotID(1)).getMarchLeft(day()), dm::FULL);
	const int receiverFull = dm::fullDailyMovement(*receiver);
	ASSERT_NO_FATAL_FAILURE(arrange(1, SlotID(1), SlotID(1), giver->id, receiver->id));
	EXPECT_EQ(receiver->movementPointsRemaining(), receiverFull);
}

TEST_F(NewHorizonsMarchLeftTest, TheLimitPacketRejectsRaisingMovement)
{
	ASSERT_NO_FATAL_FAILURE(start());
	setMovement(*receiver, 100);
	const int marched = receiver->getStack(SlotID(0)).getMarchLeft(day());
	SetMovePoints raise(receiver->id, 200);
	raise.marchLeftLimit = true;
	EXPECT_THROW(gameState()->apply(raise), std::runtime_error);
	EXPECT_EQ(receiver->movementPointsRemaining(), 100);
	SetMovePoints lower(receiver->id, 40);
	lower.marchLeftLimit = true;
	gameState()->apply(lower);
	EXPECT_EQ(receiver->movementPointsRemaining(), 40);
	// The limit is not a march: the hero's own stack keeps the march left of his last move.
	EXPECT_EQ(receiver->getStack(SlotID(0)).getMarchLeft(day()), marched);
}

TEST_F(NewHorizonsMarchLeftTest, ExchangeWarningProjectsTheAuthoritativeLimit)
{
	ASSERT_NO_FATAL_FAILURE(start());
	meet();
	march(*giver, dm::fullDailyMovement(*giver) / 4);
	const int share = giver->getStack(SlotID(1)).getMarchLeft(day());
	const int receiverFull = dm::fullDailyMovement(*receiver);
	const auto effects = dm::projectTransfers({{giver, SlotID(1), receiver, SlotID(1), false}}, day());
	ASSERT_EQ(effects.size(), 1u);
	EXPECT_EQ(effects.front().hero, receiver);
	EXPECT_EQ(effects.front().troopMarchLeft, share);
	EXPECT_EQ(effects.front().movementBefore, receiverFull);
	const int projected = effects.front().movementAfter;
	// Moving back to the giver warns nobody: the giver keeps his own movement.
	EXPECT_TRUE(dm::projectTransfers({{receiver, SlotID(0), giver, SlotID(2), false}}, day()).empty());
	ASSERT_NO_FATAL_FAILURE(arrange(1, SlotID(1), SlotID(1), giver->id, receiver->id));
	EXPECT_EQ(receiver->movementPointsRemaining(), projected);
	EXPECT_TRUE(dm::projectTransfers({{giver, SlotID(0), receiver, SlotID(0), false}}, day()).empty());
}

TEST_F(NewHorizonsMarchLeftTest, SavesKeepTodaysMarchAndOlderSavesLoadFull)
{
	ASSERT_NO_FATAL_FAILURE(start());
	march(*giver, dm::fullDailyMovement(*giver) / 4);
	const auto & marched = giver->getStack(SlotID(0));
	const int share = marched.getMarchLeft(day());
	ASSERT_LT(share, dm::FULL);

	CMemorySerializer current;
	current.oser & marched;
	CStackInstance restored(gameState().get());
	current.iser & restored;
	EXPECT_EQ(restored.getMarchLeft(day()), share);

	// A 1168 (Warcasting) or older save holds no march: its troops load fully rested.
	for(const auto version : {ESerializationVersion::NEW_HORIZONS_WARCAST, ESerializationVersion::NEW_HORIZONS_SYLVAN_AMBUSH})
	{
		CMemorySerializer older;
		older.oser.version = version;
		older.iser.version = version;
		older.oser & marched;
		CStackInstance legacy(gameState().get());
		legacy.lowerMarchLeft(day(), 1234);
		older.iser & legacy;
		EXPECT_EQ(legacy.getMarchLeft(day()), dm::FULL) << static_cast<int>(version);
		EXPECT_EQ(legacy.getCount(), marched.getCount()) << static_cast<int>(version);
	}
	EXPECT_EQ(static_cast<int>(ESerializationVersion::NEW_HORIZONS_MARCH_LEFT), 1169);
}

TEST(NewHorizonsMarchLeftRule, AHeroTakingBackHisOwnTroopsKeepsExactlyHisMovement)
{
	for(int full = 150; full <= 3200; full += 7)
		for(int remaining = 0; remaining <= full; remaining += 13)
		{
			const int share = dm::marchLeft(remaining, full);
			ASSERT_EQ(dm::movementAfterReceiving(remaining, full, share), remaining) << full << " " << remaining;
		}
}

TEST_F(NewHorizonsMarchLeftTest, HoverTextGainsTheMarchLeftOnlyForAMarchedStack)
{
	ASSERT_NO_FATAL_FAILURE(start());
	const std::string select = "Select Pikemen";
	// Fresh stacks, empty slots (nullptr) and other days keep the normal text byte for byte.
	EXPECT_EQ(dm::withMarchLeft(select, &giver->getStack(SlotID(0)), day()), select);
	EXPECT_EQ(dm::withMarchLeft("Empty", nullptr, day()), "Empty");
	EXPECT_EQ(dm::withMarchLeft("", nullptr, day()), "");
	march(*giver, dm::fullDailyMovement(*giver) / 5);
	const auto & marched = giver->getStack(SlotID(0));
	const auto left = std::to_string(dm::percentLeft(marched.getMarchLeft(day())));
	EXPECT_EQ(dm::withMarchLeft(select, &marched, day()), select + ": " + left + "% of today's march left");
	EXPECT_EQ(dm::withMarchLeft(select, &marched, day() + 1), select);
	// The receiver's untouched stack stays plain after the giver marched.
	EXPECT_EQ(dm::withMarchLeft(select, &receiver->getStack(SlotID(0)), day()), select);
}

// Anti-exploit: every way of moving marched troops keeps their march left.

TEST_F(NewHorizonsMarchLeftTest, MergingAWholeMarchedStackIntoAFreshOneKeepsTheLowerMarchLeft)
{
	ASSERT_NO_FATAL_FAILURE(start());
	meet();
	march(*giver, dm::fullDailyMovement(*giver) / 3);
	const int share = giver->getStack(SlotID(0)).getMarchLeft(day());
	ASSERT_EQ(receiver->getStack(SlotID(0)).getMarchLeft(day()), dm::FULL);
	ASSERT_NO_FATAL_FAILURE(arrange(2, SlotID(0), SlotID(0), giver->id, receiver->id)); // whole pikeman stack
	EXPECT_EQ(receiver->getStackCount(SlotID(0)), 23);
	EXPECT_EQ(receiver->getStack(SlotID(0)).getMarchLeft(day()), share);
}

TEST_F(NewHorizonsMarchLeftTest, FreshRecruitsJoiningAMarchedStackTakeItsMarchLeft)
{
	ASSERT_NO_FATAL_FAILURE(start());
	march(*giver, dm::fullDailyMovement(*giver) / 2);
	const int share = giver->getStack(SlotID(0)).getMarchLeft(day());
	ChangeStackCount recruits;
	recruits.army = giver->id;
	recruits.slot = SlotID(0);
	recruits.count = 10;
	recruits.mode = ChangeValueMode::RELATIVE;
	gameState()->apply(recruits);
	EXPECT_EQ(giver->getStackCount(SlotID(0)), 30);
	EXPECT_EQ(giver->getStack(SlotID(0)).getMarchLeft(day()), share);
	// Fresh recruits in their own slot stay fresh until merged.
	InsertNewStack fresh;
	fresh.army = giver->id;
	fresh.slot = SlotID(5);
	fresh.type = pikeman;
	fresh.count = 4;
	gameState()->apply(fresh);
	EXPECT_EQ(giver->getStack(SlotID(5)).getMarchLeft(day()), dm::FULL);
	ASSERT_NO_FATAL_FAILURE(arrange(2, SlotID(5), SlotID(0), giver->id, giver->id));
	EXPECT_EQ(giver->getStack(SlotID(0)).getMarchLeft(day()), share);
}

TEST_F(NewHorizonsMarchLeftTest, UpgradingAMarchedStackKeepsItsMarchLeft)
{
	ASSERT_NO_FATAL_FAILURE(start());
	march(*giver, dm::fullDailyMovement(*giver) / 2);
	const int share = giver->getStack(SlotID(0)).getMarchLeft(day());
	// Town and Hill Fort upgrades change the stack's type in place.
	ASSERT_TRUE(handler->changeStackType(StackLocation(giver->id, SlotID(0)),
		CreatureID(CreatureID::decode("core:halberdier")).toCreature()));
	EXPECT_EQ(giver->getStack(SlotID(0)).getCreatureID(), CreatureID(CreatureID::decode("core:halberdier")));
	EXPECT_EQ(giver->getStack(SlotID(0)).getMarchLeft(day()), share);
}

TEST_F(NewHorizonsMarchLeftTest, AThirdHeroInTheRelayIsLimitedLikeTheSecond)
{
	ASSERT_NO_FATAL_FAILURE(start());
	meet();
	march(*giver, dm::fullDailyMovement(*giver) / 4);
	const int share = giver->getStack(SlotID(1)).getMarchLeft(day());
	ASSERT_NO_FATAL_FAILURE(arrange(1, SlotID(1), SlotID(1), giver->id, receiver->id));
	handler->heroExchange(receiver->id, third->id);
	const int thirdFull = dm::fullDailyMovement(*third);
	ASSERT_NO_FATAL_FAILURE(arrange(1, SlotID(1), SlotID(1), receiver->id, third->id));
	EXPECT_EQ(third->getStack(SlotID(1)).getMarchLeft(day()), share);
	EXPECT_EQ(third->movementPointsRemaining(), dm::movementAfterReceiving(thirdFull, thirdFull, share));
}

TEST_F(NewHorizonsMarchLeftTest, GarrisonAndVisitingHeroesExchangeUnderTheRule)
{
	ASSERT_NO_FATAL_FAILURE(start());
	march(*giver, dm::fullDailyMovement(*giver) / 4);
	const int share = giver->getStack(SlotID(1)).getMarchLeft(day());
	town->setGarrisonedHero(receiver);
	town->setVisitingHero(giver);
	const int receiverFull = dm::fullDailyMovement(*receiver);
	ASSERT_NO_FATAL_FAILURE(arrange(1, SlotID(1), SlotID(1), giver->id, receiver->id));
	EXPECT_EQ(receiver->getStack(SlotID(1)).getMarchLeft(day()), share);
	EXPECT_EQ(receiver->movementPointsRemaining(), dm::movementAfterReceiving(receiverFull, receiverFull, share));
}

TEST_F(NewHorizonsMarchLeftTest, ADismissedHeroHiredAgainTheSameDayKeepsHisSpentMovement)
{
	ASSERT_NO_FATAL_FAILURE(start());
	const int full = dm::fullDailyMovement(*giver);
	march(*giver, full / 4);
	const auto heroType = giver->getHeroTypeID();
	ASSERT_TRUE(handler->removeObject(giver, player));
	auto * pooled = gameState()->getMap().tryGetFromHeroPool(heroType);
	ASSERT_NE(pooled, nullptr);
	ASSERT_TRUE(dm::hasMarchedToday(*pooled, day()));

	SetAvailableHero offer; // the tavern offers him again with a fresh army
	offer.player = player;
	offer.slotID = TavernHeroSlot::NATIVE;
	offer.roleID = TavernSlotRole::SINGLE_UNIT;
	offer.hid = heroType;
	offer.army.setCreature(SlotID(0), pikeman, 1);
	offer.replenishPoints = true;
	gameState()->apply(offer);
	EXPECT_EQ(pooled->movementPointsRemaining(), full / 4);

	// On a later day the tavern refreshes him as before.
	NewTurn turn;
	turn.day = day() + 1;
	gameState()->apply(turn);
	gameState()->apply(offer);
	EXPECT_EQ(pooled->movementPointsRemaining(), dm::fullDailyMovement(*pooled));
}

TEST_F(NewHorizonsMarchLeftTest, TheLowerPacketCarriesMarchLeftOntoAConvertedStackOnly)
{
	ASSERT_NO_FATAL_FAILURE(start());
	LowerStackMarchLeft carried;
	carried.army = giver->id;
	carried.slot = SlotID(0);
	carried.day = day();
	carried.marchLeft = 4000;
	gameState()->apply(carried);
	EXPECT_EQ(giver->getStack(SlotID(0)).getMarchLeft(day()), 4000);
	carried.marchLeft = 9000; // never rises
	gameState()->apply(carried);
	EXPECT_EQ(giver->getStack(SlotID(0)).getMarchLeft(day()), 4000);
	carried.day = day() - 1;
	EXPECT_THROW(gameState()->apply(carried), std::runtime_error);
	carried.day = day();
	carried.slot = SlotID(6);
	EXPECT_THROW(gameState()->apply(carried), std::runtime_error);
}
