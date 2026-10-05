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

	void startGame(bool useNewHorizons, bool includeTown)
	{
		useNewHorizonsRules = useNewHorizons;
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NewHorizonsTownPortalMovement")
			.playerActive(PlayerColor(0));
		if(includeTown)
			builder.town({8, 8, 0}, FactionID::CONFLUX, PlayerColor(0));
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
