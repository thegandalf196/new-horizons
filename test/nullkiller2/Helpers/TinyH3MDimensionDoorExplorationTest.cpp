/*
 * TinyH3MDimensionDoorExplorationTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "SpellPointTestUtils.h"

#include "AI/Nullkiller2/AIGateway.h"
#include "AI/Nullkiller2/Engine/FuzzyHelper.h"
#include "AI/Nullkiller2/Goals/AdventureSpellCast.h"
#include "AI/Nullkiller2/Goals/Composition.h"
#include "AI/Nullkiller2/Helpers/ExplorationHelper.h"
#include "AI/Nullkiller2/Pathfinding/AIPathfinder.h"

#include "mock/TinyH3MBuilder.h"
#include "nullkiller2/NullkillerTest.h"

#include "lib/CPlayerState.h"
#include "lib/IGameSettings.h"
#include "lib/logging/CLogger.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/spells/CSpell.h"

namespace
{
const PlayerColor PLAYER = PlayerColor(0);
const SpellID DIMENSION_DOOR = SpellID(8);

class HiddenTileVisibilityLogTarget final : public ILogTarget
{
public:
	HiddenTileVisibilityLogTarget(int3 tile, std::weak_ptr<std::atomic_size_t> warningCount)
		: tile(std::move(tile)), warningCount(std::move(warningCount))
	{
	}

	void write(const LogRecord & record) override
	{
		const auto counter = warningCount.lock();
		if(counter && record.message.find(tile.toString() + " is not visible!") != std::string::npos)
			counter->fetch_add(1);
	}

private:
	int3 tile;
	std::weak_ptr<std::atomic_size_t> warningCount;
};

TinyH3M::TinyH3MBuilder makeDimensionDoorExplorationMap(bool withDimensionDoor)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder
		.size(36, false)
		.name("DDNullkillerExploration")
		.playerActive(PLAYER)
		.hero({5, 5, 0}, HeroTypeID(0), PLAYER)
		.heroGarrison({{CreatureID(27), 1}})
		.heroPrimary(10, 10, 10, 50)
		.heroSecondarySkills({{SecondarySkill::AIR_MAGIC, 3}})
		.heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}})
		.heroSpells(withDimensionDoor ? std::vector<SpellID>{DIMENSION_DOOR} : std::vector<SpellID>{});

	return builder;
}

bool containsDimensionDoorCast(const NK2AI::Goals::TGoalVec & goals)
{
	for(const auto & goal : goals)
	{
		const auto * spellCast = dynamic_cast<const NK2AI::Goals::AdventureSpellCast *>(goal.get());
		if(spellCast && spellCast->getSpell()->getId() == DIMENSION_DOOR)
			return true;
	}

	return false;
}

class TinyH3MDimensionDoorExplorationTest : public NullkillerTest
{
public:
	bool prepareForAdventureSpellPlanning(CGHeroInstance & hero) const
	{
		const CSpell * dimensionDoor = DIMENSION_DOOR.toSpell();
		if(!dimensionDoor)
			return false;

		const int spellCost = hero.getSpellCost(dimensionDoor);
		if(spellCost <= 0)
			return false;

		setTestSpellPointTotal(&hero, spellCost);
		hero.setMovementPoints(500);
		return true;
	}

	void setTileVisible(PlayerColor player, const int3 & tile, bool visible)
	{
		ASSERT_TRUE(map()->isInTheMap(tile)) << tile.toString();
		auto * team = gameState()->getPlayerTeam(player);
		ASSERT_NE(team, nullptr);
		team->fogOfWarMap[tile] = visible ? 1 : 0;
	}
};
}

TEST_F(TinyH3MDimensionDoorExplorationTest, GeneratedDimensionDoorHeroCastsForHiddenExploration)
{
	overrideSettingBeforeInit(EGameSettings::SPELLS_DIMENSION_DOOR_TRIGGERS_GUARDS, false);
	startWithMap(makeDimensionDoorExplorationMap(true));

	auto * hero = findHeroByOwner(PLAYER);
	ASSERT_NE(hero, nullptr);
	ASSERT_TRUE(hero->hasSpellbook());
	ASSERT_TRUE(hero->spellbookContainsSpell(DIMENSION_DOOR));
	ASSERT_TRUE(prepareForAdventureSpellPlanning(*hero));

	setMapVisibility(PLAYER, false);
	setTileVisible(PLAYER, hero->visitablePos(), true);

	const auto gateway = makeGateway(PLAYER);
	NK2AI::ExplorationHelper helper(hero, gateway->nullkiller.get());

	EXPECT_TRUE(helper.canUseDimensionDoor());
	ASSERT_TRUE(helper.considerDimensionDoorExplorationTargets());
	const auto goals = helper.makeComposition()->decompose(gateway->nullkiller.get());

	EXPECT_TRUE(containsDimensionDoorCast(goals));
}

TEST_F(TinyH3MDimensionDoorExplorationTest, GeneratedHeroWithoutDimensionDoorDoesNotCast)
{
	overrideSettingBeforeInit(EGameSettings::SPELLS_DIMENSION_DOOR_TRIGGERS_GUARDS, false);
	startWithMap(makeDimensionDoorExplorationMap(false));

	auto * hero = findHeroByOwner(PLAYER);
	ASSERT_NE(hero, nullptr);
	ASSERT_TRUE(hero->hasSpellbook());
	ASSERT_FALSE(hero->spellbookContainsSpell(DIMENSION_DOOR));
	ASSERT_TRUE(prepareForAdventureSpellPlanning(*hero));

	setMapVisibility(PLAYER, false);
	setTileVisible(PLAYER, hero->visitablePos(), true);

	const auto gateway = makeGateway(PLAYER);
	NK2AI::ExplorationHelper helper(hero, gateway->nullkiller.get());

	EXPECT_FALSE(helper.canUseDimensionDoor());
	EXPECT_FALSE(helper.considerDimensionDoorExplorationTargets());
}

TEST_F(TinyH3MDimensionDoorExplorationTest, GeneratedDimensionDoorHeroDoesNotProbeHiddenTilesWhenGuardedLandingsTrigger)
{
	overrideSettingBeforeInit(EGameSettings::SPELLS_DIMENSION_DOOR_TRIGGERS_GUARDS, true);
	startWithMap(makeDimensionDoorExplorationMap(true));

	auto * hero = findHeroByOwner(PLAYER);
	ASSERT_NE(hero, nullptr);
	ASSERT_TRUE(hero->hasSpellbook());
	ASSERT_TRUE(hero->spellbookContainsSpell(DIMENSION_DOOR));
	ASSERT_TRUE(prepareForAdventureSpellPlanning(*hero));

	setMapVisibility(PLAYER, false);
	setTileVisible(PLAYER, hero->visitablePos(), true);

	const auto gateway = makeGateway(PLAYER);
	NK2AI::ExplorationHelper helper(hero, gateway->nullkiller.get());

	EXPECT_TRUE(helper.canUseDimensionDoor());
	EXPECT_FALSE(helper.considerDimensionDoorExplorationTargets());
}

TEST_F(TinyH3MDimensionDoorExplorationTest, ReconstructsHiddenPathSummaryWithoutQueryingHiddenFortification)
{
	startWithMap(makeDimensionDoorExplorationMap(false));

	auto * hero = findHeroByOwner(PLAYER);
	ASSERT_NE(hero, nullptr);
	hero->setMovementPoints(2000);
	revealMap(PLAYER);

	const auto callback = makeCallback(PLAYER);
	const auto gateway = makeGateway(callback);
	const int3 target = hero->visitablePos() + int3(1, 0, 0);
	ASSERT_TRUE(callback->isVisible(target));

	NK2AI::HeroMap<NK2AI::HeroRole> heroes;
	heroes.emplace(hero, NK2AI::MAIN);
	NK2AI::PathfinderSettings settings;
	settings.useHeroChain = false;
	gateway->nullkiller->pathfinder->updatePaths(heroes, settings);

	std::vector<NK2AI::AIPathSummary> summaries;
	gateway->nullkiller->pathfinder->calculatePathSummaries(summaries, target);
	ASSERT_FALSE(summaries.empty());
	NK2AI::AIPath visiblePath;
	ASSERT_TRUE(gateway->nullkiller->pathfinder->calculatePathInfo(visiblePath, summaries.front()));

	auto warningCount = std::make_shared<std::atomic_size_t>(0);
	CLogger::getGlobalLogger()->addTarget(
		std::make_unique<HiddenTileVisibilityLogTarget>(target, warningCount));

	setTileVisible(PLAYER, target, false);
	EXPECT_TRUE(callback->getVisitableObjs(target).empty());
	EXPECT_GT(warningCount->load(), 0u);
	warningCount->store(0);

	NK2AI::AIPath path;
	ASSERT_TRUE(gateway->nullkiller->pathfinder->calculatePathInfo(path, summaries.front()));
	EXPECT_FALSE(path.nodes.empty());
	EXPECT_EQ(path.targetTile(), target);
	EXPECT_EQ(path.targetObjectDanger, gateway->nullkiller->dangerEvaluator->evaluateDanger(target, hero));
	EXPECT_GT(path.targetObjectDanger, visiblePath.targetObjectDanger);
	EXPECT_EQ(warningCount->load(), 0u);
}
