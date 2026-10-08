/*
 * EspritDeCorpsArmySelectionTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "AI/Nullkiller2/Analyzers/ArmyManager.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "lib/battle/NewHorizonsDiscipline.h"
#include "lib/entities/hero/NewHorizonsLeadership.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/modding/CModHandler.h"
#include "mock/GameHandlerTestServer.h"
#include "nullkiller2/NullkillerTest.h"
#include "server/CGameHandler.h"

namespace
{
constexpr PlayerColor PLAYER(0);
const int3 RECEIVER_POSITION(5, 5, 0);
const int3 SOURCE_POSITION(8, 5, 0);

int countOf(const std::vector<NK2AI::SlotInfo> & army, CreatureID creature)
{
	int count = 0;
	for(const auto & slot : army)
		if(slot.creature->getId() == creature)
			count += slot.count;
	return count;
}

/// Tests the receiver-aware temporary composition in the actual NK2 selector.
class EspritDeCorpsArmySelectionTest : public NullkillerTest
{
protected:
	void SetUp() override
	{
		NullkillerTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		{
			GTEST_SKIP() << "Requires the native New Horizons preset";
		}
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
		auto & perks = rules["skills"][newHorizonsDiscipline::SKILL]["perks"].Vector();
		const auto entry = std::ranges::find_if(perks, [](const auto & perk)
		{
			return perk["id"].String() == newHorizonsDiscipline::ESPRIT_DE_CORPS;
		});
		if(entry == perks.end())
			throw std::runtime_error("Missing Esprit de Corps registry entry");
		RecordProperty("esprit_registry_status", (*entry)["effect"]["status"].String());
		// Before activation only the planned row is enabled locally. Acquisition
		// still uses its real rank requirements, legal offer and server handler.
		if((*entry)["effect"]["status"].String() == "planned")
			(*entry)["effect"]["status"].String() = "active";
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(rules));
	}

	void selectEsprit(CGHeroInstance * hero, CGameHandler & handler)
	{
		ASSERT_EQ(hero->getPerkSkillRank(newHorizonsDiscipline::SKILL), MasteryLevel::BASIC);
		const auto rankLookup = [hero](const std::string & skill) { return hero->getPerkSkillRank(skill); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto entry = std::ranges::find_if(offers, [](const auto & offer)
			{
				return offer.selection.skillId == newHorizonsDiscipline::SKILL
					&& offer.selection.perkId == newHorizonsDiscipline::ESPRIT_DE_CORPS;
			});
			if(entry == offers.end())
				continue;
			handler.levelUpHero(hero, offers, std::distance(offers.begin(), entry), seed, false);
			ASSERT_TRUE(newHorizonsDiscipline::hasEspritDeCorps(hero));
			return;
		}
		FAIL() << "No legitimate Basic Esprit de Corps offer";
	}
};
}

TEST_F(EspritDeCorpsArmySelectionTest, SelectedReceiverChangesBestArmyButSourceOnlyDoesNot)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	const CreatureID centaur(CreatureID::decode("core:centaur"));
	const CreatureID goblin(CreatureID::decode("core:goblin"));
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PLAYER)
		.hero(RECEIVER_POSITION, HeroTypeID(HeroTypeID::decode("core:christian")), PLAYER)
		.heroExperience(0).heroGarrison({{pikeman, 15}, {centaur, 10}})
		.hero(SOURCE_POSITION, HeroTypeID(HeroTypeID::decode("core:adela")), PLAYER)
		.heroExperience(0).heroGarrison({{goblin, 2}, {pikeman, 1}});
	startWithMap(std::move(builder));
	auto * receiver = findHeroAt(RECEIVER_POSITION);
	auto * source = findHeroAt(SOURCE_POSITION);
	ASSERT_NE(receiver, nullptr);
	ASSERT_NE(source, nullptr);
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	gameState()->actingPlayers.insert(PLAYER);
	const SecondarySkill discipline(SecondarySkill::decode(newHorizonsDiscipline::SKILL));
	for(auto * hero : {receiver, source})
	{
		if(hero->getPerkSkillRank(newHorizonsDiscipline::SKILL) == MasteryLevel::NONE)
			handler.levelUpHero(hero, discipline, false);
		ASSERT_EQ(hero->getPerkSkillRank(newHorizonsDiscipline::SKILL), MasteryLevel::BASIC);
		ASSERT_FALSE(newHorizonsDiscipline::hasEspritDeCorps(hero));
		for(const auto & slot : hero->Slots())
		{
			const auto capacity = hero->getLeadershipSlotCapacity(slot.second->getCreatureID());
			ASSERT_TRUE(capacity);
			ASSERT_TRUE(capacity->accepts(slot.second->getCount()));
		}
	}
	// Two Goblins let the donor retain its final stack after transferring one;
	// the weak third faction must not be blocked merely by last-stack admission.
	const auto gateway = makeGateway(PLAYER);
	const auto * manager = gateway->nullkiller->armyManager.get();
	const auto unchangedReadback = [&]()
	{
		const auto before = gameState()->saveToMemory();
		const auto result = manager->getBestArmy(receiver, receiver, source, TerrainId::NONE, source);
		EXPECT_EQ(gameState()->saveToMemory(), before);
		return result;
	};
	const auto ordinary = unchangedReadback();
	ASSERT_EQ(countOf(ordinary, goblin), 0);
	ASSERT_GT(countOf(ordinary, pikeman), 0);
	ASSERT_GT(countOf(ordinary, centaur), 0);
	ASSERT_NO_FATAL_FAILURE(selectEsprit(source, handler));
	const auto sourceOnly = unchangedReadback();
	EXPECT_EQ(countOf(sourceOnly, goblin), 0);
	EXPECT_EQ(countOf(sourceOnly, pikeman), countOf(ordinary, pikeman));
	EXPECT_EQ(countOf(sourceOnly, centaur), countOf(ordinary, centaur));
	ASSERT_FALSE(newHorizonsDiscipline::hasEspritDeCorps(receiver));
	ASSERT_NO_FATAL_FAILURE(selectEsprit(receiver, handler));
	for(int sample = 0; sample < 3; ++sample)
	{
		const auto selected = unchangedReadback();
		EXPECT_EQ(countOf(selected, goblin), 1);
		EXPECT_EQ(countOf(selected, pikeman), countOf(ordinary, pikeman));
		EXPECT_EQ(countOf(selected, centaur), countOf(ordinary, centaur));
	}
	EXPECT_EQ(receiver->getStackCount(SlotID(0)), 15);
	EXPECT_EQ(receiver->getStackCount(SlotID(1)), 10);
	EXPECT_EQ(source->getStackCount(SlotID(0)), 2);
	EXPECT_EQ(source->getStackCount(SlotID(1)), 1);
}
