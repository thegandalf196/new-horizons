/*
 * MarchLeftChainTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 */
#include "StdInc.h"

#include "AI/Nullkiller2/AIGateway.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "AI/Nullkiller2/Analyzers/HeroManager.h"
#include "AI/Nullkiller2/Pathfinding/AIPathfinder.h"
#include "AI/Nullkiller2/Pathfinding/Actors.h"
#include "AI/Nullkiller2/Helpers/MarchLeftTransfer.h"
#include "lib/gameState/CGameState.h"
#include "lib/networkPacks/PacksForClient.h"
#include "mock/TinyH3MBuilder.h"
#include "nullkiller2/NullkillerTest.h"
#include "../../OriginalDataRequirement.h"

#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/pathfinder/NewHorizonsMarchLeft.h"

namespace
{
const PlayerColor PLAYER(0);
const int3 DONOR_START(2, 5, 0);
const int3 CARRIER_START(4, 5, 0);

NK2AI::ChainActor actor(int land, int sea, bool movable = true)
{
	NK2AI::ChainActor result;
	result.isMovable = movable;
	result.marchLandMovement = land;
	result.marchSeaMovement = sea;
	return result;
}
}

TEST(Nullkiller2_MarchLeftExchange, MarchedTroopsLimitTheCarrierOnlyOnTheDayTheyMarched)
{
	// A ChainActor holds a ResourceSet, which sizes itself from the loaded resource types.
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const auto carrier = actor(1800, 1500);
	const auto donor = actor(2000, 1600);
	// The spec example: the donor used 1500 of 2000, the fresh carrier keeps 450.
	EXPECT_EQ(NK2AI::marchLeftExchangeMovement(carrier, EPathfindingLayer::LAND, 1800,
		donor, EPathfindingLayer::LAND, 500, true), 450);
	// Troops that waited for a later meeting day are fresh again.
	EXPECT_EQ(NK2AI::marchLeftExchangeMovement(carrier, EPathfindingLayer::LAND, 1800,
		donor, EPathfindingLayer::LAND, 500, false), 1800);
	// A carrier already below the limit is unchanged; the limit never raises.
	EXPECT_EQ(NK2AI::marchLeftExchangeMovement(carrier, EPathfindingLayer::LAND, 300,
		donor, EPathfindingLayer::LAND, 500, true), 300);
	// Sea movement uses the sea budgets of both heroes.
	EXPECT_EQ(NK2AI::marchLeftExchangeMovement(carrier, EPathfindingLayer::SAIL, 1500,
		donor, EPathfindingLayer::SAIL, 400, true), 375);
	// Town garrisons and dwellings never march; legacy-rule heroes have no march to limit.
	EXPECT_EQ(NK2AI::marchLeftExchangeMovement(carrier, EPathfindingLayer::LAND, 1800,
		actor(2000, 1600, false), EPathfindingLayer::LAND, 0, true), 1800);
	EXPECT_EQ(NK2AI::marchLeftExchangeMovement(carrier, EPathfindingLayer::LAND, 1800,
		actor(0, 0), EPathfindingLayer::LAND, 0, true), 1800);
	EXPECT_EQ(NK2AI::marchLeftExchangeMovement(actor(0, 0), EPathfindingLayer::LAND, 1800,
		donor, EPathfindingLayer::LAND, 0, true), 1800);
}

class Nullkiller2_MarchLeftChain : public NullkillerTest
{
protected:
	CGHeroInstance * donor = nullptr;
	CGHeroInstance * carrier = nullptr;

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
	}

	void start()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder
			.size(36, false)
			.name("NK2MarchLeftChain")
			.playerActive(PLAYER)
			.hero(DONOR_START, HeroTypeID(HeroTypeID::decode("core:valeska")), PLAYER)
			.hero(CARRIER_START, HeroTypeID(HeroTypeID::decode("core:orrin")), PLAYER);
		startWithMap(std::move(builder));
		donor = findHeroAt(DONOR_START);
		carrier = findHeroAt(CARRIER_START);
		ASSERT_NE(donor, nullptr);
		ASSERT_NE(carrier, nullptr);
		ASSERT_TRUE(newHorizonsMarchLeft::appliesTo(*donor));
		ASSERT_TRUE(newHorizonsMarchLeft::appliesTo(*carrier));
		donor->clearSlots();
		carrier->clearSlots();
		ASSERT_TRUE(donor->setCreature(SlotID(0), CreatureID(CreatureID::decode("core:archangel")), 20));
		ASSERT_TRUE(carrier->setCreature(SlotID(0), CreatureID(CreatureID::decode("core:pikeman")), 1));
		carrier->setMovementPoints(newHorizonsMarchLeft::fullDailyMovement(*carrier));
	}

	static void updatePaths(NK2AI::Nullkiller * ai)
	{
		ai->heroManager->update();
		NK2AI::PathfinderSettings settings;
		settings.useHeroChain = true;
		ai->pathfinder->updatePaths(ai->getHeroesForPathfinding(), settings);
	}

	/// Whether the carrier, carrying the donor's army, reaches the tile today.
	bool chainReachesToday(const NK2AI::Nullkiller * ai, const int3 & tile) const
	{
		const auto paths = ai->pathfinder->getPathInfo(tile, ai->isObjectGraphAllowed());
		return std::ranges::any_of(paths, [this](const NK2AI::AIPath & path)
		{
			return path.targetHero == carrier && path.containsHero(donor) && path.turn() == 0;
		});
	}

	/// The farthest tile east of the carrier that he reaches alone using at most 60% of his day.
	int3 targetTile(const NK2AI::Nullkiller * ai) const
	{
		int3 best = int3(-1, -1, -1);
		for(int x = CARRIER_START.x + 1; x < 34; ++x)
		{
			const int3 tile(x, CARRIER_START.y, 0);
			for(const auto & path : ai->pathfinder->getPathInfo(tile, ai->isObjectGraphAllowed()))
				if(path.targetHero == carrier && !path.containsHero(donor) && path.turn() == 0
					&& path.movementCost() <= 0.6f)
					best = tile;
		}
		return best;
	}
};

TEST_F(Nullkiller2_MarchLeftChain, PlannerKeepsFreshExchangesButDropsTheTiredRelay)
{
	ASSERT_NO_FATAL_FAILURE(start());
	const int donorFull = newHorizonsMarchLeft::fullDailyMovement(*donor);

	donor->setMovementPoints(donorFull);
	const auto fresh = makeGateway(PLAYER);
	updatePaths(fresh->nullkiller.get());
	const int3 target = targetTile(fresh->nullkiller.get());
	ASSERT_TRUE(target.isValid());
	// Control: fresh troops carried by the carrier still reach the target today.
	EXPECT_TRUE(chainReachesToday(fresh->nullkiller.get(), target));

	// The donor marched almost all day before handing over: the relay cannot reach it today.
	donor->setMovementPoints(donorFull / 8);
	const auto tired = makeGateway(PLAYER);
	updatePaths(tired->nullkiller.get());
	EXPECT_FALSE(chainReachesToday(tired->nullkiller.get(), target));
	const auto solo = tired->nullkiller->pathfinder->getPathInfo(target, tired->nullkiller->isObjectGraphAllowed());
	EXPECT_TRUE(std::ranges::any_of(solo, [this](const NK2AI::AIPath & path)
	{
		return path.targetHero == carrier && !path.containsHero(donor) && path.turn() == 0;
	}));
}

TEST_F(Nullkiller2_MarchLeftChain, TheMainHeroIsNotStrandedByAnIncidentalExchange)
{
	ASSERT_NO_FATAL_FAILURE(start());
	const int day = gameState()->getCalendar().getCurrentDay();
	const int donorFull = newHorizonsMarchLeft::fullDailyMovement(*donor);
	const auto spend = [this](int remaining)
	{
		SetMovePoints move(donor->id, remaining);
		gameState()->apply(move);
	};
	spend(donorFull);
	EXPECT_FALSE(NK2AI::marchLeftTransfer::strandsReceiver(*carrier, *donor, day)); // fresh troops
	spend(donorFull / 2);
	EXPECT_FALSE(NK2AI::marchLeftTransfer::strandsReceiver(*carrier, *donor, day)); // half a day left
	spend(donorFull / 20);
	EXPECT_TRUE(NK2AI::marchLeftTransfer::strandsReceiver(*carrier, *donor, day)); // 5% left
	carrier->setMovementPoints(0);
	EXPECT_FALSE(NK2AI::marchLeftTransfer::strandsReceiver(*carrier, *donor, day)); // already done today
}
