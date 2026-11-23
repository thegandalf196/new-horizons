/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "nullkiller2/NullkillerTest.h"
#include "AI/Nullkiller2/AIUtility.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "AI/Nullkiller2/Engine/PriorityEvaluator.h"
#include "AI/Nullkiller2/Pathfinding/AIPathfinder.h"
#include "lib/GameConstants.h"
#include "lib/entities/hero/NewHorizonsRampartSpecialties.h"
#include "lib/mapObjects/CGCreature.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/CGTownInstance.h"
#include "lib/modding/CModHandler.h"
#include "lib/networkPacks/PacksForClient.h"
#include "mock/GameHandlerTestServer.h"
#include "server/CGameHandler.h"

namespace
{
const PlayerColor PLAYER(0);
CreatureID creature(const char * name) { return CreatureID(CreatureID::decode(name)); }

/// The adventure AI uses Kyrre's Elusive through the shared per-hero guard query and
/// values Jenova's weekly Patroness collection when it ranks her own towns.
class RampartAdventureSpecialtyAITest : public NullkillerTest
{
protected:
	CGHeroInstance * specialist = nullptr;
	CGHeroInstance * other = nullptr;
	CGCreature * guard = nullptr;
	CGTownInstance * town = nullptr;
	std::unique_ptr<NK2AI::AIGateway> gateway;

	void SetUp() override
	{
		NullkillerTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires curated New Horizons preset";
	}

	void mapLoaded(CMap * loaded) override
	{
		NullkillerTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
	}

	void prepare(const char * hero)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PLAYER)
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(hero)), PLAYER).heroGarrison({{creature("core:centaur"), 1}})
			.hero({24, 24, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PLAYER).heroGarrison({{creature("core:pikeman"), 1}})
			.monster({6, 4, 0}, creature("core:pikeman"), 20, static_cast<int8_t>(CGCreature::Character::HOSTILE))
			.town({15, 15, 0}, FactionID::RAMPART, PLAYER).townGarrison({});
		startWithMap(std::move(builder));
		specialist = findHeroAt({5, 5, 0});
		other = findHeroAt({24, 24, 0});
		guard = findFirst<CGCreature>();
		town = expectAt<CGTownInstance>({15, 15, 0});
		ASSERT_TRUE(specialist && other && guard && town);
		revealMap(PLAYER);
		gateway = makeGateway(PLAYER);
	}

	std::optional<int3> guardedNeighbour() const
	{
		const auto source = specialist->visitablePos();
		for(int dx = -1; dx <= 1; ++dx)
			for(int dy = -1; dy <= 1; ++dy)
			{
				const auto tile = source + int3(dx, dy, 0);
				if((dx || dy) && map()->isInTheMap(tile) && tile != guard->visitablePos()
					&& map()->guardingCreaturePosition(tile) == guard->visitablePos() && !map()->getTile(tile).blocked())
					return tile;
			}
		return std::nullopt;
	}
};
}

TEST_F(RampartAdventureSpecialtyAITest, KyrresRoutesBesideACoreGuardCarryNoDanger)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:kyrre"));
	const auto tile = guardedNeighbour();
	ASSERT_TRUE(tile);
	auto * ai = gateway->nullkiller.get();
	EXPECT_EQ(ai->dangerEvaluator->evaluateDanger(*tile, specialist), 0u) << "Elusive: the guard does not attack her";
	EXPECT_GT(ai->dangerEvaluator->evaluateDanger(*tile, other), 0u);
	EXPECT_GT(ai->dangerEvaluator->evaluateDanger(guard->visitablePos(), specialist), 0u)
		<< "Attacking the guard itself is still a fight";
	// The shared pathfinder the AI plans with lets her continue past the guarded tile.
	EXPECT_TRUE(ai->cc->getGuardingCreatures(*tile, specialist).empty());
	EXPECT_FALSE(ai->cc->getGuardingCreatures(*tile, other).empty());
}

TEST_F(RampartAdventureSpecialtyAITest, JenovaValuesAndVisitsHerTownsUntilShePaidThisWeek)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:jenova"));
	auto * ai = gateway->nullkiller.get();
	NK2AI::RewardEvaluator reward(ai);
	const auto collection = newHorizonsHeroes::patronessCollection(*specialist, *town);
	ASSERT_GT(collection[EGameResID::GOLD], 0);
	EXPECT_TRUE(NK2AI::shouldVisit(ai, specialist, town)) << "Her uncollected own town is a target";
	EXPECT_FALSE(NK2AI::shouldVisit(ai, other, town));
	EXPECT_GE(reward.getGoldReward(town, specialist), collection[EGameResID::GOLD]);
	EXPECT_EQ(reward.getGoldReward(town, other), 0);
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	handler.heroVisitCastle(town, specialist);
	EXPECT_FALSE(NK2AI::shouldVisit(ai, specialist, town)) << "Collected this week";
	EXPECT_EQ(reward.getGoldReward(town, specialist), 0);
}

TEST_F(RampartAdventureSpecialtyAITest, JenovaDoesNotTargetTheTownSheIsAlreadyInside)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:jenova"));
	auto * ai = gateway->nullkiller.get();
	NK2AI::RewardEvaluator reward(ai);
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	// Inside without an arrival collection (as after a load or a capture that skipped it).
	HeroVisitCastle inside;
	inside.hid = specialist->id;
	inside.tid = town->id;
	inside.startVisit = true;
	handler.sendAndApply(inside);
	ASSERT_EQ(town->getVisitingHero(), specialist);
	ASSERT_FALSE(newHorizonsHeroes::patronessCollection(*specialist, *town).empty());
	EXPECT_TRUE(newHorizonsHeroes::patronessOnNextEntry(*specialist, *town).empty());
	EXPECT_FALSE(NK2AI::shouldVisit(ai, specialist, town)) << "Staying inside collects nothing";
	EXPECT_EQ(reward.getGoldReward(town, specialist), 0);
}
