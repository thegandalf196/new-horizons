/*
 * NewHorizonsBoneAndSteelTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../mock/TinyH3MBuilder.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/entities/hero/NewHorizonsNecromancy.h"
#include "../../../lib/entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../server/queries/BattleQueries.h"
#include "../../../server/queries/QueriesProcessor.h"

#include <algorithm>

namespace
{
// 0 absent, 1 owned but unbuilt, 2 owned and built, 3 foreign and built.
class NewHorizonsBoneAndSteelTest : public BattleTestFixture,
	public ::testing::WithParamInterface<int>
{
protected:
	CGTownInstance * town = nullptr;
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("BasicBoneAndSteel")
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(72), PlayerColor(0)).heroGarrison({{CreatureID(0), 1}})
			.hero({7, 7, 0}, HeroTypeID(1), PlayerColor(1)).heroGarrison({{CreatureID(0), 1}});
		if(GetParam() != 0)
			builder.town({10, 10, 0}, FactionID::NECROPOLIS,
				GetParam() == 3 ? PlayerColor(1) : PlayerColor(0)).townGarrison({});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		makeNeutral(attackerSideHero);
		makeNeutral(defenderSideHero);
		const auto towns = gameState()->getMap().getObjects<CGTownInstance>();
		if(GetParam() == 0)
			ASSERT_TRUE(towns.empty());
		else
		{
			ASSERT_EQ(towns.size(), 1u);
			town = towns.front();
			town->removeAllBuildings();
			town->addBuilding(BuildingID::DWELL_LVL_1);
			if(GetParam() >= 2)
				town->addBuilding(BuildingID::DWELL_LVL_1_UP);
			ASSERT_TRUE(town->getTown()->buildings.contains(BuildingID::DWELL_LVL_1_UP));
			auto & offered = town->creatures.front().second;
			offered = {creatureByName("core:skeleton")};
			if(GetParam() >= 2)
				offered.push_back(creatureByName("core:skeletonWarrior"));
		}
		gameHandler->onAdvInterfaceReady(PlayerColor(0));
		gameHandler->onAdvInterfaceReady(PlayerColor(1));
	}

	int64_t count(CreatureID creature) const
	{
		int64_t result = 0;
		for(const auto & [slot, stack] : attackerSideHero->Slots())
			if(stack->getCreatureID() == creature)
				result += stack->getCount();
		return result;
	}
};
}

TEST_P(NewHorizonsBoneAndSteelTest, BasicPerkRequiresAnActuallyOwnedBuiltUpgradeForRaisedForm)
{
	const auto skill = SecondarySkill(SecondarySkill::decode(newHorizonsNecromancy::SKILL_ID));
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	const auto & rules = attackerSideHero->getPerkState().rules;
	ASSERT_TRUE(newHorizonsNecromancy::policy(rules).has_value());
	const auto & perks = rules["skills"][newHorizonsNecromancy::SKILL_ID]["perks"].Vector();
	const auto row = std::ranges::find_if(perks, [](const auto & perk)
		{ return perk["id"].String() == newHorizonsNecromancy::MASTER_OF_BONES_ID; });
	ASSERT_NE(row, perks.end());
	EXPECT_EQ((*row)["name"].String(), "Bone and Steel");
	EXPECT_EQ((*row)["requires"].String(), "basic");
	ASSERT_TRUE(attackerSideHero->getPerkState().canAcquirePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::MASTER_OF_BONES_ID, MasteryLevel::BASIC));
	attackerSideHero->applyPerkSelection({newHorizonsNecromancy::SKILL_ID, newHorizonsNecromancy::MASTER_OF_BONES_ID});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::MASTER_OF_BONES_ID));
	ASSERT_EQ(attackerSideHero->getSecSkillLevel(skill), MasteryLevel::BASIC);
	const auto base = creatureByName("core:skeleton");
	const auto upgraded = creatureByName("core:skeletonWarrior");
	attackerSideHero->clearSlots();
	defenderSideHero->clearSlots();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), base, 1));
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), 10));
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	gameHandler->battles->cheatBattleVictory(PlayerColor(0));
	for(const auto player : {PlayerColor(0), PlayerColor(1)})
	{
		const auto query = gameHandler->queries->topQuery(player);
		if(query && query->getType() == QueryType::BattleDialog)
			ASSERT_TRUE(gameHandler->queryReply(query->queryID, 0, player));
	}
	ASSERT_EQ(gameState()->getBattle(PlayerColor(0)), nullptr);
	EXPECT_EQ(count(base), GetParam() == 2 ? 1 : 2);
	EXPECT_EQ(count(upgraded), GetParam() == 2 ? 1 : 0);
	RecordProperty("dwellingScenario", std::to_string(GetParam()));
	RecordProperty("skillRank", "basic");
	RecordProperty("policy", "current");
}

INSTANTIATE_TEST_SUITE_P(CurrentPolicy, NewHorizonsBoneAndSteelTest, ::testing::Values(0, 1, 2, 3));
