/*
 * NewHorizonsNecromancyV2DeliveryTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HistoricalNecromancy087Fixture.h"
#include "../../mock/TinyMapGameTest.h"
#include "../../mock/GameHandlerTestServer.h"
#include "../../SpellPointTestUtils.h"
#include "../../../lib/entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/NewHorizonsRaisedArmy.h"

namespace
{
class NewHorizonsNecromancyV2DeliveryTest : public TinyMapGameTest,
	public ::testing::WithParamInterface<bool>
{
protected:
	Services * gameServices() override { return LIBRARY; }

	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		auto rules = GetParam() ? JsonNode(JsonPath::builtin("config/newHorizonsPerks"))
			: necromancy087Test::rules();
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, rules);
	}

	void startScenario()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.town({12, 12, 0}, FactionID::NECROPOLIS, PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:isra")), PlayerColor(0))
			.town({26, 26, 0}, FactionID::CASTLE, PlayerColor(1));
		startWithMap(std::move(builder));
		hero = findHeroByOwner(PlayerColor(0));
		town = findFirst<CGTownInstance>();
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(town, nullptr);
		ASSERT_EQ(town->getOwner(), PlayerColor(0));
		ASSERT_EQ(town->getFactionID(), FactionID::NECROPOLIS);
		ASSERT_TRUE(newHorizonsNecromancy::policy(hero->getPerkState().rules));
		ASSERT_EQ(newHorizonsNecromancy::policy(hero->getPerkState().rules)->version, GetParam() ? 2 : 1);
		hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(newHorizonsNecromancy::SKILL_ID)),
			MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(hero, 0);
		ASSERT_GE(hero->manaLimit(), 50);
		hero->applyPerkSelection({newHorizonsNecromancy::SKILL_ID, GetParam()
			? newHorizonsNecromancy::BLACK_HARVEST_ID : newHorizonsNecromancy::BONE_COLLECTOR_ID});
		hero->applyPerkSelection({newHorizonsNecromancy::SKILL_ID, GetParam()
			? newHorizonsNecromancy::BONE_COLLECTOR_ID : newHorizonsNecromancy::OSSUARY_ID});
		town->clearSlots();
		fillHero(6);
		result.winner = BattleSide::ATTACKER;
		result.necromancyEligibilityCaptured = true;
		result.necromancyEligibleCasualties[BattleSide::DEFENDER][pikeman] = 100;
	}

	void fillHero(int slots)
	{
		hero->clearSlots();
		for(int slot = 0; slot < slots; ++slot)
			ASSERT_TRUE(hero->setCreature(SlotID(slot), pikeman, 1));
	}

	newHorizonsNecromancy::PreparedBattleNecromancy prepared() const
	{
		return newHorizonsNecromancy::prepareBattleNecromancy(result, {}, 0, *hero,
			gameState()->getPlayerState(PlayerColor(0)));
	}

	CGHeroInstance * hero = nullptr;
	CGTownInstance * town = nullptr;
	CreatureID pikeman{CreatureID::decode("core:pikeman")};
	BattleResult result;
};
}

TEST_P(NewHorizonsNecromancyV2DeliveryTest, AcceptedFreshOutputAndFullArmyFallbackFollowCapturedPolicy)
{
	ASSERT_NO_FATAL_FAILURE(startScenario());
	auto initial = prepared();
	ASSERT_TRUE(initial.ordinary.applied);
	ASSERT_GT(initial.ordinary.skeletonsRaised, 0);
	EXPECT_EQ(initial.ossuary, !GetParam());
	if(GetParam())
	{
		const auto captured = newHorizonsNecromancy::policy(hero->getPerkState().rules);
		ASSERT_TRUE(captured);
		EXPECT_EQ(captured->boneCollectorRaiseBonusPercent, 25);
		EXPECT_EQ(captured->soulHarvesterManaCap, 30);
		EXPECT_TRUE(initial.ordinary.boneCollector);
		ASSERT_GT(initial.ordinary.skeletonsRaised, 30);
		EXPECT_EQ(initial.ordinary.manaRecovered, 30);
	}
	const auto * owner = gameState()->getPlayerState(PlayerColor(0));
	auto accepted = newHorizonsNecromancy::selectRaisedArmyDelivery(*hero, owner, initial);
	ASSERT_TRUE(accepted);
	ASSERT_EQ(accepted->army, hero);
	EXPECT_EQ(accepted->summary.ossuaryTown, ObjectInstanceID::NONE);
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	newHorizonsNecromancy::applyRaisedArmy(handler, *accepted->army, accepted->additions);
	const auto slot = hero->getSlotFor(CreatureID::SKELETON);
	ASSERT_TRUE(slot.validSlot());
	EXPECT_EQ(hero->getStackCount(slot), accepted->summary.skeletonsRaised);
	EXPECT_EQ(town->stacksCount(), 0);

	// All seven hero slots now contain legal, unrelated troops. The owned town
	// is available and empty, so rejection cannot be attributed to missing room.
	ASSERT_NO_FATAL_FAILURE(fillHero(GameConstants::ARMY_SIZE));
	if(GetParam())
	{
		auto & state = const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState());
		std::erase_if(state.selected, [](const auto & selection)
		{
			return selection.skillId == newHorizonsNecromancy::SKILL_ID
				&& selection.perkId == newHorizonsNecromancy::BONE_COLLECTOR_ID;
		});
		hero->applyPerkSelection({newHorizonsNecromancy::SKILL_ID, newHorizonsNecromancy::OSSUARY_ID});
		ASSERT_TRUE(hero->hasActivePerk(newHorizonsNecromancy::SKILL_ID, newHorizonsNecromancy::OSSUARY_ID));
		EXPECT_EQ(newHorizonsNecromancy::deadMansLuckBonus(hero, CreatureID::SKELETON), 3);
	}
	const auto fullArmy = prepared();
	EXPECT_EQ(fullArmy.ossuary, !GetParam());
	const auto fallback = newHorizonsNecromancy::selectRaisedArmyDelivery(*hero, owner, fullArmy);
	if(GetParam())
	{
		EXPECT_FALSE(fallback);
		EXPECT_EQ(town->stacksCount(), 0);
		EXPECT_EQ(hero->stacksCount(), GameConstants::ARMY_SIZE);
	}
	else
	{
		ASSERT_TRUE(fallback);
		EXPECT_EQ(fallback->army, town->getUpperArmy());
		EXPECT_EQ(fallback->summary.ossuaryTown, town->id);
		newHorizonsNecromancy::applyRaisedArmy(handler, *fallback->army, fallback->additions);
		const auto townSlot = town->getSlotFor(CreatureID::SKELETON);
		ASSERT_TRUE(townSlot.validSlot());
		EXPECT_EQ(town->getStackCount(townSlot), fallback->summary.skeletonsRaised);
	}
}

INSTANTIATE_TEST_SUITE_P(HistoricalV1AndCurrentV2, NewHorizonsNecromancyV2DeliveryTest, ::testing::Bool());
