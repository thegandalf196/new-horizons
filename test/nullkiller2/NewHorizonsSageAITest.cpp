/*
 * NewHorizonsSageAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "nullkiller2/NullkillerTest.h"
#include "AI/Nullkiller2/AIUtility.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "AI/Nullkiller2/Pathfinding/AIPathfinder.h"
#include "lib/GameConstants.h"
#include "lib/GameLibrary.h"
#include "lib/GameSettings.h"
#include "lib/CPlayerState.h"
#include "lib/callback/CCallback.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/CGTownInstance.h"
#include "lib/mapObjects/NewHorizonsAcademicStudy.h"
#include "lib/mapObjects/NewHorizonsSage.h"
#include "lib/mapping/CMap.h"
#include "lib/modding/CModHandler.h"
#include "lib/spells/CSpellHandler.h"
#include "server/CGameHandler.h"
#include "mock/GameHandlerTestServer.h"

namespace
{
const PlayerColor PLAYER(0);
SpellID named(const char * key) { return SpellID(SpellID::decode(key)); }
SecondarySkill skill(const char * key) { return SecondarySkill(SecondarySkill::decode(key)); }

class NewHorizonsSageAITest : public NullkillerTest
{
protected:
	CGHeroInstance * hero = nullptr;
	CGHeroInstance * other = nullptr;
	CGHeroInstance * foreign = nullptr;
	CGTownInstance * town = nullptr;
	std::unique_ptr<NK2AI::AIGateway> gateway;
	void SetUp() override
	{
		NullkillerTest::SetUp();
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER).playerActive(PlayerColor(1))
			.hero({4, 5, 0}, HeroTypeID(0), PLAYER).heroGarrison({{CreatureID(27), 10}})
			.hero({4, 20, 0}, HeroTypeID(1), PLAYER)
			.hero({28, 28, 0}, HeroTypeID(2), PlayerColor(1))
			.town({12, 12, 0}, FactionID(FactionID::decode("core:castle")), PLAYER);
		startWithMap(std::move(builder));
		revealMap(PLAYER);
		hero = findHeroAt({4, 5, 0});
		other = findHeroAt({4, 20, 0});
		foreign = findHeroAt({28, 28, 0});
		town = findFirst<CGTownInstance>();
		ASSERT_NE(hero, nullptr); ASSERT_NE(other, nullptr); ASSERT_NE(foreign, nullptr); ASSERT_NE(town, nullptr);
		GameHandlerTestServer server(gameState(), PLAYER);
		CGameHandler handler(server, gameState());
		for(auto * current : {hero, other, foreign})
		{
			current->setSecSkillLevel(skill("new-horizons:learning"), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
			current->setSecSkillLevel(skill("new-horizons:wisdom"), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			current->setSecSkillLevel(skill("new-horizons:sorceryMagic"), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
			if(!current->hasSpellbook())
				ASSERT_TRUE(handler.giveHeroNewArtifact(current, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK));
			handler.changeSpells(current, false, current->getSpellsInSpellbook());
		}
		for(int tier = 1; tier <= 4; ++tier)
			town->addBuilding(BuildingID(BuildingID::MAGES_GUILD_1 + tier - 1));
		town->removeBuilding(BuildingID::MAGES_GUILD_5);
		map()->allowedSpells = {named("core:magicArrow"), named("core:implosion")};
		town->spells.assign(GameConstants::SPELL_LEVELS, {});
		town->newHorizonsMageGuildVisibleSpells.assign(GameConstants::SPELL_LEVELS, 0);
		town->newHorizonsMageGuildVisibleSpellSchools.assign(GameConstants::SPELL_LEVELS,
			{SpellSchool(SpellSchool::decode("new-horizons:sorcery"))});
		town->spells[0] = {named("core:magicArrow")};
		town->newHorizonsMageGuildVisibleSpells[0] = 1;
		gateway = makeGateway(PLAYER);
	}
	void select(bool wisdom, CGHeroInstance * current = nullptr)
	{
		if(!current) current = hero;
		if(wisdom)
		{
			current->applyPerkSelection({"new-horizons:wisdom", "new-horizons:wisdom.intelligence"});
			current->applyPerkSelection({"new-horizons:wisdom", "new-horizons:wisdom.sage"});
			ASSERT_TRUE(current->hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.sage"));
		}
		else
		{
			current->applyPerkSelection({"new-horizons:learning", "new-horizons:learning.eagleEye"});
			current->applyPerkSelection({"new-horizons:learning", "new-horizons:learning.fieldStudy"});
			current->applyPerkSelection({"new-horizons:learning", "new-horizons:learning.sage"});
			ASSERT_TRUE(newHorizonsSage::learningSelected(*current));
		}
		ASSERT_FALSE(current->hasActivePerk("new-horizons:learning", "new-horizons:learning.academicStudy"));
	}
	std::vector<const CGObjectInstance *> candidates()
	{
		auto * ai = gateway->nullkiller.get();
		ai->heroManager->update(); ai->armyManager->update();
		NK2AI::PathfinderSettings settings; settings.useHeroChain = false;
		ai->pathfinder->updatePaths(ai->getHeroesForPathfinding(), settings);
		ai->memory->visitableObjs = {town->id};
		ai->memory->alreadyVisited = {town->id};
		ai->objectClusterizer->reset(); ai->objectClusterizer->clusterize();
		return ai->objectClusterizer->getNearbyObjects();
	}
};
}

TEST_F(NewHorizonsSageAITest, LearningSageWithoutAcademicReachesVisitedTownAndAcceptedVisitSpendsOnlyItsReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(select(false));
	ASSERT_EQ(newHorizonsLearning::academicStudyExperience(*hero, *town), 0);
	ASSERT_EQ(newHorizonsSage::selectSpell(*hero, *town), named("core:implosion"));
	EXPECT_TRUE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, town));
	EXPECT_FALSE(NK2AI::shouldVisit(gateway->nullkiller.get(), other, town));
	const auto before = candidates();
	EXPECT_NE(std::ranges::find(before, town), before.end());
	GameHandlerTestServer server(gameState(), PLAYER); CGameHandler handler(server, gameState());
	handler.heroVisitCastle(town, hero);
	EXPECT_TRUE(hero->spellbookContainsSpell(named("core:implosion")));
	EXPECT_TRUE(hero->getNewHorizonsSageGuildVisits().contains(town->id));
	EXPECT_FALSE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, town));
	EXPECT_TRUE(candidates().empty());
}

TEST_F(NewHorizonsSageAITest, WisdomSageWithoutAcademicAlsoReachesThePlanner)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(select(true));
	ASSERT_TRUE(newHorizonsSage::wisdomReveal(*hero, *town));
	ASSERT_TRUE(newHorizonsSage::hasVisitReward(*hero, *town));
	EXPECT_TRUE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, town));
	const auto available = candidates();
	EXPECT_NE(std::ranges::find(available, town), available.end());
}

TEST_F(NewHorizonsSageAITest, UnselectedOrEmptyCatalogDoesNotPermitBlanketTownRevisits)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, town));
	EXPECT_TRUE(candidates().empty());
	ASSERT_NO_FATAL_FAILURE(select(false));
	map()->allowedSpells = {named("core:magicArrow")};
	EXPECT_FALSE(newHorizonsSage::selectSpell(*hero, *town));
	EXPECT_FALSE(newHorizonsSage::hasVisitReward(*hero, *town));
	EXPECT_FALSE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, town));
	EXPECT_TRUE(candidates().empty());
}

TEST_F(NewHorizonsSageAITest, AlliedTownIsEligibleButEnemyCaptureKeepsItsOrdinaryPolicy)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(select(false));
	GameHandlerTestServer server(gameState(), PLAYER); CGameHandler handler(server, gameState());
	handler.setOwner(town, PlayerColor(1));
	EXPECT_EQ(gameState()->getPlayerRelations(PLAYER, PlayerColor(1)), PlayerRelations::ENEMIES);
	EXPECT_FALSE(newHorizonsSage::hasVisitReward(*hero, *town));
	EXPECT_TRUE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, town)); // ordinary capture remains legal
	const auto team = gameState()->players.at(PLAYER).team;
	gameState()->players.at(PlayerColor(1)).team = team;
	gameState()->teams.at(team).players.insert(PlayerColor(1));
	ASSERT_EQ(gameState()->getPlayerRelations(PLAYER, PlayerColor(1)), PlayerRelations::ALLIES);
	EXPECT_TRUE(newHorizonsSage::hasVisitReward(*hero, *town));
	EXPECT_TRUE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, town));
}

TEST_F(NewHorizonsSageAITest, ForeignSelectedHeroCannotEnableOwnHerosVisitedTownBypass)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(select(false, foreign));
	EXPECT_FALSE(newHorizonsSage::hasVisitReward(*foreign, *town)); // enemy endpoint
	const auto team = gameState()->players.at(PLAYER).team;
	gameState()->players.at(PlayerColor(1)).team = team;
	gameState()->teams.at(team).players.insert(PlayerColor(1));
	ASSERT_TRUE(newHorizonsSage::hasVisitReward(*foreign, *town));
	EXPECT_FALSE(NK2AI::shouldVisit(gateway->nullkiller.get(), hero, town));
	EXPECT_TRUE(candidates().empty());
}
