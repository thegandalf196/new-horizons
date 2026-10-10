/*
 * NewHorizonsRecruitersContactsAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "nullkiller2/NullkillerTest.h"
#include "AI/Nullkiller2/AIUtility.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "AI/Nullkiller2/Pathfinding/AIPathfinder.h"
#include "lib/CSkillHandler.h"
#include "lib/GameConstants.h"
#include "lib/GameLibrary.h"
#include "lib/IGameSettings.h"
#include "lib/callback/CCallback.h"
#include "lib/mapObjects/CGDwelling.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/ObjectTemplate.h"
#include "lib/mapObjectConstructors/CObjectClassesHandler.h"
#include "lib/mapObjectConstructors/AObjectTypeHandler.h"
#include "lib/mapObjects/NewHorizonsRecruitersContacts.h"
#include "lib/mapping/CMap.h"
#include "lib/modding/CModHandler.h"
#include "lib/networkPacks/PacksForClient.h"
#include "server/CGameHandler.h"
#include "mock/GameHandlerTestServer.h"

namespace
{
const PlayerColor PLAYER(0);
constexpr auto RECRUITMENT = "new-horizons:recruitment";
constexpr auto CONTACTS = "new-horizons:recruitment.recruiterSContacts";

class NewHorizonsRecruitersContactsAITest : public NullkillerTest
{
protected:
	CGHeroInstance * hero = nullptr;
	CGHeroInstance * otherHero = nullptr;
	CGDwelling * dwelling = nullptr;
	std::unique_ptr<NK2AI::AIGateway> gateway;
	void SetUp() override
	{
		NullkillerTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		map->overrideGameSetting(EGameSettings::CREATURES_NEW_HORIZONS_CATEGORIES, JsonNode(JsonPath::builtin("config/newHorizonsCreatureCategories")));
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void select(CGHeroInstance * target)
	{
		target->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(RECRUITMENT)), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		target->applyPerkSelection({RECRUITMENT, "new-horizons:recruitment.volunteerNetwork"});
		target->applyPerkSelection({RECRUITMENT, CONTACTS});
		ASSERT_TRUE(target->hasActivePerk(RECRUITMENT, CONTACTS));
	}
	void prepare(bool selected = true)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER)
			.hero({4, 5, 0}, HeroTypeID(0), PLAYER)
			.heroGarrison({{CreatureID(27), 10}})
			.hero({4, 20, 0}, HeroTypeID(1), PLAYER)
			.dwelling({8, 8, 0}, MapObjectSubID(56), PLAYER);
		startWithMap(std::move(builder));
		revealMap(PLAYER);
		hero = findHeroAt({4, 5, 0});
		otherHero = findHeroAt({4, 20, 0});
		dwelling = dynamic_cast<CGDwelling *>(findObjectAt({8, 8, 0}));
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(otherHero, nullptr);
		ASSERT_NE(dwelling, nullptr);
		dwelling->clearSlots();
		dwelling->creatures = {{0, {CreatureID(CreatureID::decode("core:pikeman"))}}};
		if(selected)
			select(hero);
		gateway = makeGateway(PLAYER);
	}
	std::vector<const CGObjectInstance *> candidates()
	{
		auto * ai = gateway->nullkiller.get();
		ai->heroManager->update();
		ai->armyManager->update();
		NK2AI::PathfinderSettings settings;
		settings.useHeroChain = false;
		ai->pathfinder->updatePaths(ai->getHeroesForPathfinding(), settings);
		ai->memory->visitableObjs = {dwelling->id};
		ai->memory->alreadyVisited = {dwelling->id};
		ai->objectClusterizer->reset();
		ai->objectClusterizer->clusterize();
		return ai->objectClusterizer->getNearbyObjects();
	}
	void spend()
	{
		const auto award = newHorizonsRecruitment::recruitersContactsAward(*hero, *dwelling, 0);
		ASSERT_TRUE(award);
		SetAvailableCreatures stock;
		stock.tid = dwelling->id;
		stock.creatures = dwelling->creatures;
		stock.creatures[award->row].first = award->amount;
		stock.recruitersContacts = SetAvailableCreatures::RecruitersContactsReceipt{hero->id, 0, award->row, award->amount};
		GameHandlerTestServer server(gameState(), PLAYER);
		CGameHandler handler(server, gameState());
		handler.sendAndApply(stock);
		// Emulate subsequent ordinary recruitment, not another Contacts grant.
		stock.creatures[award->row].first = 0;
		stock.recruitersContacts.reset();
		handler.sendAndApply(stock);
	}
};
}

TEST_F(NewHorizonsRecruitersContactsAITest, VisitedEmptySingleRowDwellingReachesThePlannerAndUsedHeroDoesNot)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * ai = gateway->nullkiller.get();
	EXPECT_TRUE(NK2AI::shouldVisit(ai, hero, dwelling));
	EXPECT_FALSE(NK2AI::shouldVisit(ai, otherHero, dwelling));
	const auto available = candidates();
	EXPECT_NE(std::ranges::find(available, dwelling), available.end());
	ASSERT_NO_FATAL_FAILURE(spend());
	EXPECT_FALSE(NK2AI::shouldVisit(ai, hero, dwelling));
	EXPECT_TRUE(candidates().empty());
}

TEST_F(NewHorizonsRecruitersContactsAITest, VisitedMultiRowDwellingBypassRequiresAnEligibleOwnHero)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	// Use a registered multi-row dwelling, including its real subtype/template.
	// Keeping the single-row subtype56 would ask the planner for nonexistent20::56.
	const auto conflux = MapObjectSubID::decode(Obj::CREATURE_GENERATOR4, "core:elementalConflux");
	ASSERT_GE(conflux, 0);
	const auto handler = LIBRARY->objtypeh->getHandlerFor(Obj::CREATURE_GENERATOR4, MapObjectSubID(conflux));
	ASSERT_NE(handler, nullptr);
	const auto templates = handler->getTemplates();
	ASSERT_FALSE(templates.empty());
	const auto visitablePosition = dwelling->visitablePos();
	auto & map = gameState()->getMap();
	map.hideObject(dwelling);
	dwelling->pos = visitablePosition + templates.front()->getVisitableOffset();
	dwelling->appearance = templates.front();
	dwelling->ID = Obj::CREATURE_GENERATOR4;
	dwelling->subID = MapObjectSubID(conflux);
	map.showObject(dwelling);
	ASSERT_EQ(dwelling->visitablePos(), visitablePosition);
	ASSERT_EQ(dwelling->ID, Obj::CREATURE_GENERATOR4);
	ASSERT_EQ(dwelling->subID, MapObjectSubID(conflux));
	dwelling->creatures = {{0, {CreatureID(CreatureID::decode("core:pikeman"))}},
		{0, {CreatureID(CreatureID::decode("core:pikeman"))}}};
	ASSERT_EQ(dwelling->creatures.size(), 2);
	auto * ai = gateway->nullkiller.get();
	EXPECT_TRUE(NK2AI::shouldVisit(ai, hero, dwelling));
	EXPECT_FALSE(NK2AI::shouldVisit(ai, otherHero, dwelling));
	const auto available = candidates();
	EXPECT_NE(std::ranges::find(available, dwelling), available.end());
	ASSERT_NO_FATAL_FAILURE(spend());
	EXPECT_TRUE(candidates().empty());
	ASSERT_NO_FATAL_FAILURE(select(otherHero));
	EXPECT_FALSE(NK2AI::shouldVisit(ai, hero, dwelling));
	EXPECT_TRUE(NK2AI::shouldVisit(ai, otherHero, dwelling));
	const auto otherAvailable = candidates();
	EXPECT_NE(std::ranges::find(otherAvailable, dwelling), otherAvailable.end());
}

TEST_F(NewHorizonsRecruitersContactsAITest, MissingPerkAndUnaffordableGeneratedRowDoNotBypassVisitSafeguards)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	auto * ai = gateway->nullkiller.get();
	EXPECT_FALSE(NK2AI::shouldVisit(ai, hero, dwelling));
	EXPECT_TRUE(candidates().empty());
	ASSERT_NO_FATAL_FAILURE(select(hero));
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	handler.giveResource(PLAYER, EGameResID::GOLD, -gateway->cc->getResourceAmount()[EGameResID::GOLD]);
	ASSERT_TRUE(newHorizonsRecruitment::recruitersContactsAward(*hero, *dwelling, 0));
	EXPECT_FALSE(NK2AI::shouldVisit(ai, hero, dwelling));
	EXPECT_TRUE(candidates().empty());
	EXPECT_EQ(hero->getNewHorizonsRecruitersContactsLastWeek(), -1);
}
