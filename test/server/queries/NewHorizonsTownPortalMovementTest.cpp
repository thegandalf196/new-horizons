/*
 * NewHorizonsTownPortalMovementTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"

#include "../../mock/GameHandlerTestServer.h"
#include "../../mock/TinyMapGameTest.h"
#include "../../SpellPointTestUtils.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/bonuses/Bonus.h"
#include <algorithm>
#include "../../../lib/IGameSettings.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/adventure/TownPortalEffect.h"
#include "../../../server/CGameHandler.h"

namespace
{
class NewHorizonsTownPortalMovementTest : public TinyMapGameTest
{
protected:
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			useNewHorizonsRules ? JsonNode(JsonPath::builtin("config/newHorizonsMagic")) : JsonNode());
	}

	void startGame(bool useNewHorizons, bool includeTown, bool includeSecondTown = false, PlayerColor secondOwner = PlayerColor(0))
	{
		useNewHorizonsRules = useNewHorizons;
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NewHorizonsTownPortalMovement")
			.playerActive(PlayerColor(0));
		if(includeTown)
			builder.town({8, 8, 0}, FactionID::CONFLUX, PlayerColor(0));
		if(includeSecondTown)
		{
			if(secondOwner != PlayerColor(0))
				builder.playerActive(secondOwner);
			builder.town({20, 20, 0}, FactionID::CONFLUX, secondOwner);
		}
		builder.hero({15, 15, 0}, HeroTypeID(0), PlayerColor(0))
			.heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}});
		startWithMap(std::move(builder));

		hero = findFirst<CGHeroInstance>();
		town = findFirst<CGTownInstance>();
		ASSERT_NE(hero, nullptr);
		if(includeTown)
			ASSERT_NE(town, nullptr);
		else
			EXPECT_EQ(town, nullptr);

		server = std::make_unique<GameHandlerTestServer>(gameState(), PlayerColor(0));
		gameHandler = std::make_unique<CGameHandler>(*server, gameState());
		hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(hero, 100);
		hero->setMovementPoints(1000);
		hero->addSpellToSpellbook(SpellID(SpellID::TOWN_PORTAL));
	}

	void forceEffectRank(int rank)
	{
		hero->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::SPELL,
			BonusSource::OTHER, rank, BonusSourceID(), BonusSubtypeID(townPortal)));
		ASSERT_EQ(hero->getSpellSchoolLevel(townPortal.toSpell()), rank);
	}

	SpellCastEnvironment * spellEnvironment()
	{
		return dynamic_cast<SpellCastEnvironment *>(gameHandler->spellcastEnvironment());
	}

	const SpellID townPortal{SpellID::TOWN_PORTAL};
	bool useNewHorizonsRules = true;
	CGHeroInstance * hero = nullptr;
	CGTownInstance * town = nullptr;
	std::unique_ptr<GameHandlerTestServer> server;
	std::unique_ptr<CGameHandler> gameHandler;
};
}

TEST_F(NewHorizonsTownPortalMovementTest, NewHorizonsCastExhaustsRemainingMovement)
{
	ASSERT_NO_FATAL_FAILURE(startGame(true, true));
	ASSERT_NE(spellEnvironment(), nullptr);
	ASSERT_TRUE(hero->spellbookContainsSpell(townPortal));
	ASSERT_TRUE(newHorizonsMagic::isAdventureSpell(hero->getMagicRules(), townPortal));
	const auto * effect = townPortal.toSpell()->getAdventureMechanics().getEffectAs<TownPortalEffect>(hero);
	ASSERT_NE(effect, nullptr);
	EXPECT_EQ(effect->getMovementPointsTaken(hero, 1000), 1000);

	AdventureSpellCastParameters parameters;
	parameters.caster = hero;
	parameters.pos = int3();
	const auto manaBefore = hero->getManaAvailable();
	ASSERT_TRUE(townPortal.toSpell()->adventureCast(spellEnvironment(), parameters));

	EXPECT_EQ(hero->visitablePos(), town->visitablePos());
	EXPECT_EQ(hero->movementPointsRemaining(), 0u);
	EXPECT_EQ(hero->getManaAvailable(), manaBefore - hero->getSpellCost(townPortal.toSpell()));
	EXPECT_TRUE(hero->hasNewHorizonsAdventureSpellCastToday());
}

TEST_F(NewHorizonsTownPortalMovementTest, LegacyCastKeepsConfiguredFixedDeduction)
{
	ASSERT_NO_FATAL_FAILURE(startGame(false, true));
	ASSERT_NE(spellEnvironment(), nullptr);
	ASSERT_TRUE(hero->spellbookContainsSpell(townPortal));
	ASSERT_FALSE(newHorizonsMagic::isAdventureSpell(hero->getMagicRules(), townPortal));
	const auto * effect = townPortal.toSpell()->getAdventureMechanics().getEffectAs<TownPortalEffect>(hero);
	ASSERT_NE(effect, nullptr);
	EXPECT_EQ(effect->getMovementPointsTaken(hero, 1000), 300);

	AdventureSpellCastParameters parameters;
	parameters.caster = hero;
	parameters.pos = int3();
	const auto manaBefore = hero->getManaAvailable();
	ASSERT_TRUE(townPortal.toSpell()->adventureCast(spellEnvironment(), parameters));

	EXPECT_EQ(hero->visitablePos(), town->visitablePos());
	EXPECT_EQ(hero->movementPointsRemaining(), 700u);
	EXPECT_EQ(hero->getManaAvailable(), manaBefore - hero->getSpellCost(townPortal.toSpell()));
	EXPECT_FALSE(hero->hasNewHorizonsAdventureSpellCastToday());
}

TEST_F(NewHorizonsTownPortalMovementTest, NoTownCancellationDoesNotSpendMovementOrMana)
{
	ASSERT_NO_FATAL_FAILURE(startGame(true, false));
	ASSERT_NE(spellEnvironment(), nullptr);
	ASSERT_TRUE(hero->spellbookContainsSpell(townPortal));
	const auto manaBefore = hero->getManaAvailable();
	const auto movementBefore = hero->movementPointsRemaining();

	AdventureSpellCastParameters parameters;
	parameters.caster = hero;
	parameters.pos = int3();
	EXPECT_TRUE(townPortal.toSpell()->adventureCast(spellEnvironment(), parameters));

	EXPECT_EQ(hero->getManaAvailable(), manaBefore);
	EXPECT_EQ(hero->movementPointsRemaining(), movementBefore);
	EXPECT_FALSE(hero->hasNewHorizonsAdventureSpellCastToday());
}

class NewHorizonsTownPortalRankTest : public NewHorizonsTownPortalMovementTest, public ::testing::WithParamInterface<int>
{
};

TEST_P(NewHorizonsTownPortalRankTest, EveryRankIgnoresFartherDestinationAndUsesNearestOwnedTown)
{
	const auto rank = GetParam();
	SCOPED_TRACE(rank);
	ASSERT_NO_FATAL_FAILURE(startGame(true, true, true));
	ASSERT_NO_FATAL_FAILURE(forceEffectRank(rank));
	const auto * effect = townPortal.toSpell()->getAdventureMechanics().getEffectAs<TownPortalEffect>(hero);
	ASSERT_NE(effect, nullptr);
	EXPECT_FALSE(effect->townSelectionAllowed(hero));
	const auto pool = effect->getControlledTowns(*gameState(), hero);
	ASSERT_EQ(pool.size(), 2u);
	const auto * nearest = effect->findNearestTown(hero->visitablePos(), pool);
	ASSERT_NE(nearest, nullptr);
	ASSERT_NE(nearest, town);
	const auto mana = hero->getManaAvailable();
	AdventureSpellCastParameters parameters;
	parameters.caster = hero;
	parameters.pos = town->visitablePos();
	ASSERT_TRUE(townPortal.toSpell()->adventureCast(spellEnvironment(), parameters));
	EXPECT_EQ(hero->visitablePos(), nearest->visitablePos());
	EXPECT_EQ(hero->movementPointsRemaining(), 0u);
	EXPECT_EQ(hero->getManaAvailable(), mana - 50);
	EXPECT_TRUE(hero->hasNewHorizonsAdventureSpellCastToday());
}

INSTANTIATE_TEST_SUITE_P(AllRanks, NewHorizonsTownPortalRankTest, ::testing::Values(0, 1, 2, 3));

TEST_F(NewHorizonsTownPortalMovementTest, AdvancedWithoutDestinationCompletesWithoutTownPicker)
{
	ASSERT_NO_FATAL_FAILURE(startGame(true, true, true));
	ASSERT_NO_FATAL_FAILURE(forceEffectRank(2));
	const auto * effect = townPortal.toSpell()->getAdventureMechanics().getEffectAs<TownPortalEffect>(hero);
	ASSERT_NE(effect, nullptr);
	const auto * nearest = effect->findNearestTown(hero->visitablePos(), effect->getControlledTowns(*gameState(), hero));
	ASSERT_NE(nearest, nullptr);
	AdventureSpellCastParameters parameters;
	parameters.caster = hero;
	parameters.pos = int3();
	ASSERT_TRUE(townPortal.toSpell()->adventureCast(spellEnvironment(), parameters));
	EXPECT_EQ(hero->visitablePos(), nearest->visitablePos());
	EXPECT_EQ(hero->movementPointsRemaining(), 0u);
	EXPECT_TRUE(hero->hasNewHorizonsAdventureSpellCastToday());
}

TEST_F(NewHorizonsTownPortalMovementTest, LegacyExpertStillUsesExplicitFartherDestination)
{
	ASSERT_NO_FATAL_FAILURE(startGame(false, true, true));
	ASSERT_NO_FATAL_FAILURE(forceEffectRank(3));
	const auto * effect = townPortal.toSpell()->getAdventureMechanics().getEffectAs<TownPortalEffect>(hero);
	ASSERT_NE(effect, nullptr);
	ASSERT_TRUE(effect->townSelectionAllowed(hero));
	AdventureSpellCastParameters parameters;
	parameters.caster = hero;
	parameters.pos = town->visitablePos();
	ASSERT_TRUE(townPortal.toSpell()->adventureCast(spellEnvironment(), parameters));
	EXPECT_EQ(hero->visitablePos(), town->visitablePos());
	EXPECT_EQ(hero->movementPointsRemaining(), 800u);
	EXPECT_FALSE(hero->hasNewHorizonsAdventureSpellCastToday());
}

TEST_F(NewHorizonsTownPortalMovementTest, NearestOwnedPoolExcludesCloserEnemyTown)
{
	ASSERT_NO_FATAL_FAILURE(startGame(true, true, true, PlayerColor(1)));
	const auto * effect = townPortal.toSpell()->getAdventureMechanics().getEffectAs<TownPortalEffect>(hero);
	ASSERT_NE(effect, nullptr);
	const auto pool = effect->getControlledTowns(*gameState(), hero);
	ASSERT_EQ(pool.size(), 1u);
	EXPECT_EQ(pool.front()->getOwner(), hero->getOwner());
	AdventureSpellCastParameters parameters;
	parameters.caster = hero;
	parameters.pos = int3();
	ASSERT_TRUE(townPortal.toSpell()->adventureCast(spellEnvironment(), parameters));
	EXPECT_EQ(hero->visitablePos(), pool.front()->visitablePos());
}

TEST_F(NewHorizonsTownPortalMovementTest, SharedResolverKeepsFirstEntryOnSquaredDistanceTie)
{
	ASSERT_NO_FATAL_FAILURE(startGame(true, true, true));
	const auto towns = gameState()->getPlayerState(PlayerColor(0))->getTowns();
	const std::vector<const CGTownInstance *> pool(towns.begin(), towns.end());
	ASSERT_EQ(pool.size(), 2u);
	const auto first = pool.front()->visitablePos();
	const auto second = pool.back()->visitablePos();
	const int3 midpoint((first.x + second.x) / 2, (first.y + second.y) / 2, first.z);
	ASSERT_EQ(first.dist2dSQ(midpoint), second.dist2dSQ(midpoint));
	EXPECT_EQ(TownPortalEffect::findNearestTown(midpoint, pool), pool.front());
	auto reversed = pool;
	std::reverse(reversed.begin(), reversed.end());
	EXPECT_EQ(TownPortalEffect::findNearestTown(midpoint, reversed), reversed.front());
}

TEST_F(NewHorizonsTownPortalMovementTest, OccupiedNearestCancelsWithoutChoosingFartherFreeTown)
{
	ASSERT_NO_FATAL_FAILURE(startGame(true, true, true));
	ASSERT_NO_FATAL_FAILURE(forceEffectRank(3));
	const auto * effect = townPortal.toSpell()->getAdventureMechanics().getEffectAs<TownPortalEffect>(hero);
	ASSERT_NE(effect, nullptr);
	const auto * nearest = effect->findNearestTown(hero->visitablePos(), effect->getControlledTowns(*gameState(), hero));
	ASSERT_NE(nearest, nullptr);
	ASSERT_NE(nearest, town);
	auto * occupied = const_cast<CGTownInstance *>(nearest);
	occupied->setVisitingHero(hero);
	const auto position = hero->visitablePos();
	const auto mana = hero->getManaAvailable();
	const auto movement = hero->movementPointsRemaining();
	AdventureSpellCastParameters parameters;
	parameters.caster = hero;
	parameters.pos = town->visitablePos();
	EXPECT_TRUE(townPortal.toSpell()->adventureCast(spellEnvironment(), parameters));
	EXPECT_EQ(hero->visitablePos(), position);
	EXPECT_EQ(hero->getManaAvailable(), mana);
	EXPECT_EQ(hero->movementPointsRemaining(), movement);
	EXPECT_FALSE(hero->hasNewHorizonsAdventureSpellCastToday());
}
