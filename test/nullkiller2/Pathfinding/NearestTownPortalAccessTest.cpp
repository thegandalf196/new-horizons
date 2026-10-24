/*
 * NearestTownPortalAccessTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "nullkiller2/NullkillerTest.h"
#include "SpellPointTestUtils.h"
#include "server/battles/FullGameSnapshotTypes.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "AI/Nullkiller2/Pathfinding/AIPathfinder.h"
#include "AI/Nullkiller2/Pathfinding/Actions/TownPortalAction.h"
#include "lib/GameConstants.h"
#include "lib/GameLibrary.h"
#include "lib/GameSettings.h"
#include "lib/bonuses/Bonus.h"
#include "lib/callback/CCallback.h"
#include "lib/logging/CLogger.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/CGTownInstance.h"
#include "lib/modding/CModHandler.h"
#include "lib/serializer/CMemorySerializer.h"
#include "lib/spells/CSpell.h"
#include "lib/spells/ISpellMechanics.h"
#include "lib/spells/adventure/TownPortalEffect.h"
#include <atomic>

namespace
{
const PlayerColor OBSERVER(0);
const PlayerColor OWNER(1);
const int3 HERO_POSITION(5, 5, 0);
const int3 NEAR_TOWN_POSITION(15, 12, 0);
const int3 FAR_TOWN_POSITION(27, 27, 0);

class PlayerAccessLogTarget final : public ILogTarget
{
	std::weak_ptr<std::atomic_size_t> count;
public:
	explicit PlayerAccessLogTarget(std::weak_ptr<std::atomic_size_t> count) : count(std::move(count)) {}
	void write(const LogRecord & record) override
	{
		if(record.level == ELogLevel::ERROR && record.message == "Cannot access player 1 info!")
		{
			if(const auto counter = count.lock())
				counter->fetch_add(1);
		}
	}
};

std::vector<std::byte> serializedRng(const CRandomGenerator & rng)
{
	CMemorySerializer memory;
	memory.oser & rng;
	return memory.extractBuffer();
}

bool hasPortal(const NK2AI::AIPath & path)
{
	return std::ranges::any_of(path.nodes, [](const NK2AI::AIPathNodeInfo & node)
	{
		return node.specialAction
			&& dynamic_cast<const NK2AI::AIPathfinding::TownPortalAction *>(node.specialAction.get());
	});
}

class NearestTownPortalAccessTest : public NullkillerTest
{
protected:
	CGHeroInstance * hero = nullptr;
	CGTownInstance * nearTown = nullptr;
	CGTownInstance * farTown = nullptr;

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void prepare(bool caster)
	{
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
		const SpellID portal(SpellID::decode("core:townPortal"));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("Nearest Town Portal access")
			.playerActive(OBSERVER).playerActive(OWNER)
			.hero({30, 5, 0}, HeroTypeID(1), OBSERVER)
			.town({30, 8, 0}, FactionID::CASTLE, OBSERVER)
			.town(NEAR_TOWN_POSITION, FactionID::CASTLE, OWNER).townGarrison({})
			.town(FAR_TOWN_POSITION, FactionID::CASTLE, OWNER).townGarrison({})
			.hero(HERO_POSITION, HeroTypeID(0), OWNER).heroGarrison({{CreatureID(27), 1}})
			.heroPrimary(10, 10, 10, 20).heroSpells(caster ? std::vector{portal} : std::vector<SpellID>{})
			.heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}});
		startWithMap(std::move(builder));
		hero = findHeroByOwner(OWNER);
		nearTown = expectAt<CGTownInstance>(NEAR_TOWN_POSITION);
		farTown = expectAt<CGTownInstance>(FAR_TOWN_POSITION);
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(nearTown, nullptr);
		ASSERT_NE(farTown, nullptr);
		setTestSpellPointTotal(hero, 200);
		hero->setMovementPoints(2000);
		const auto * effect = portal.toSpell()->getAdventureMechanics().getEffectAs<TownPortalEffect>(hero);
		ASSERT_NE(effect, nullptr);
		ASSERT_TRUE(effect->usesNearestControlledTown(hero));
		ASSERT_EQ(hero->canCastThisSpell(portal.toSpell()), caster);
		ASSERT_GE(hero->getManaAvailable(), hero->getSpellCost(portal.toSpell()));
		revealMap(OBSERVER);
		revealMap(OWNER);
		// A real impassable wall makes any route to these towns a teleport.
		for(int y = 0; y < map()->height; ++y)
			map()->getTile({10, y, 0}).terrainType = ETerrainId::ROCK;
		// Object visibility tests every covered picture tile, including unblocked tiles.
		for(int y = 0; y < nearTown->getHeight(); ++y)
		{
			for(int x = 0; x < nearTown->getWidth(); ++x)
			{
				const auto tile = nearTown->anchorPos() - int3(x, y, 0);
				if(map()->isInTheMap(tile) && nearTown->coveringAt(tile))
					gameState()->getPlayerTeam(OBSERVER)->fogOfWarMap[tile] = 0;
			}
		}
	}

	void update(NK2AI::AIGateway & gateway)
	{
		NK2AI::HeroMap<NK2AI::HeroRole> heroes;
		heroes.emplace(hero, NK2AI::MAIN);
		NK2AI::PathfinderSettings settings;
		settings.useHeroChain = false;
		settings.useDimensionDoor = false;
		settings.mainTurnDistanceLimit = settings.scoutTurnDistanceLimit = 1;
		gateway.nullkiller->pathfinder->updatePaths(heroes, settings);
	}

	void checkDeniedForecast(bool caster)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(caster));
		auto gateway = makeGateway(OBSERVER);
		const auto & callback = gateway->nullkiller->cc;
		ASSERT_FALSE(callback->canGetFullInfo(hero));
		ASSERT_EQ(callback->getPlayerState(OWNER, false), nullptr);
		ASSERT_EQ(callback->getObj(hero->id, false), hero);
		const auto visibleTowns = callback->getTownsInfo(false);
		ASSERT_NE(std::ranges::find(visibleTowns, farTown), visibleTowns.end());
		ASSERT_EQ(farTown->getOwner(), hero->getOwner());
		ASSERT_NE(callback->getPlayerRelations(hero->getOwner(), farTown->getOwner()), PlayerRelations::ENEMIES);
		ASSERT_FALSE(callback->isVisible(nearTown));
		ASSERT_EQ(std::ranges::find(visibleTowns, nearTown), visibleTowns.end());
		auto errors = std::make_shared<std::atomic_size_t>(0);
		CLogger::getGlobalLogger()->addTarget(std::make_unique<PlayerAccessLogTarget>(errors));
		// Calibrate the exact denied-access observer without changing logger settings.
		EXPECT_EQ(callback->getPlayerState(OWNER), nullptr);
		ASSERT_EQ(errors->load(), 1u);
		errors->store(0);
		const auto world = gameState()->saveToMemory();
		const auto gameRng = serializedRng(dynamic_cast<CRandomGenerator &>(*getRNG()));
		const auto defaultRng = serializedRng(CRandomGenerator::getDefault());
		update(*gateway);
		EXPECT_EQ(errors->load(), 0u);
		const auto ordinary = gateway->nullkiller->pathfinder->getPathInfo(hero->visitablePos() + int3(1, 0, 0));
		ASSERT_FALSE(ordinary.empty()) << "Visible enemy ordinary movement must remain forecastable";
		EXPECT_TRUE(std::ranges::any_of(ordinary, [this](const NK2AI::AIPath & path)
			{ return path.targetHero == hero && !hasPortal(path); }));
		for(const auto * town : {nearTown, farTown})
			for(const auto & path : gateway->nullkiller->pathfinder->getPathInfo(town->visitablePos()))
				EXPECT_FALSE(hasPortal(path));
		EXPECT_EQ(gameState()->saveToMemory(), world);
		EXPECT_EQ(serializedRng(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
		EXPECT_EQ(serializedRng(CRandomGenerator::getDefault()), defaultRng);
		EXPECT_EQ(hero->visitablePos(), HERO_POSITION - hero->getVisitableOffset());
		EXPECT_EQ(hero->movementPointsRemaining(), 2000);
	}

	void checkAccessibleForecast(bool allied)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(true));
		if(allied)
		{
			gameState()->getPlayerTeam(OWNER)->players.erase(OWNER);
			gameState()->getPlayerTeam(OBSERVER)->players.insert(OWNER);
			gameState()->getPlayerState(OWNER)->team = gameState()->getPlayerState(OBSERVER)->team;
			revealMap(OBSERVER);
			const SpellID portal(SpellID::decode("core:townPortal"));
			const auto * effect = portal.toSpell()->getAdventureMechanics().getEffectAs<TownPortalEffect>(hero);
			ASSERT_NE(effect, nullptr);
			// Non-own actors start with full-day capacity, not the fixture current MP.
			hero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MOVEMENT,
				BonusSource::OTHER, effect->getMovementPointsRequired(), BonusSourceID(),
				BonusSubtypeID(BonusCustomSubtype::heroMovementLand), BonusValueType::ADDITIVE_VALUE));
			ASSERT_GE(hero->movementPointsLimit(), effect->getMovementPointsRequired());
			hero->setMovementPoints(hero->movementPointsLimit());
		}
		auto gateway = makeGateway(allied ? OBSERVER : OWNER);
		ASSERT_TRUE(gateway->nullkiller->cc->canGetFullInfo(hero));
		update(*gateway);
		const auto nearestPaths = gateway->nullkiller->pathfinder->getPathInfo(nearTown->visitablePos());
		const auto portal = std::ranges::find_if(nearestPaths, hasPortal);
		ASSERT_NE(portal, nearestPaths.end());
		EXPECT_EQ(portal->turn(), 0);
		std::vector<NK2AI::AIPathSummary> summaries;
		gateway->nullkiller->pathfinder->calculatePathSummaries(summaries, nearTown->visitablePos());
		EXPECT_TRUE(std::ranges::any_of(summaries, [](const NK2AI::AIPathSummary & summary)
		{
			return summary.node && summary.node->turns == 0 && summary.node->moveRemains == 0
				&& dynamic_cast<const NK2AI::AIPathfinding::TownPortalAction *>(summary.node->specialAction.get());
		}));
		for(const auto & path : gateway->nullkiller->pathfinder->getPathInfo(farTown->visitablePos()))
			if(path.turn() == 0)
			{
				EXPECT_FALSE(hasPortal(path)) << "A farther visible town cannot replace the nearest owned town";
			}
	}
};
}

TEST_F(NearestTownPortalAccessTest, VisibleEnemyNoncasterDoesNotRequestPrivateTownRoster)
{
	checkDeniedForecast(false);
}

TEST_F(NearestTownPortalAccessTest, VisibleEnemyCasterDoesNotInferNearestTownFromPartialPool)
{
	checkDeniedForecast(true);
}

TEST_F(NearestTownPortalAccessTest, OwnCasterKeepsNearestControlledTownAndExhaustedMovement)
{
	checkAccessibleForecast(false);
}

TEST_F(NearestTownPortalAccessTest, AlliedCasterKeepsAccessibleCompleteRosterAndExhaustedMovement)
{
	checkAccessibleForecast(true);
}
