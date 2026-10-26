/*
 * NewHorizonsNecromancyHellfireTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "HistoricalNecromancyFixture.h"
#include "../../mock/TinyH3MBuilder.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/entities/hero/NewHorizonsNecromancy.h"
#include "../../../lib/entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

namespace
{
// Current Boneward, current unselected, historical Dark Conversion respectively.
class NewHorizonsNecromancyHellfireTest : public BattleTestFixture,
	public ::testing::WithParamInterface<int>
{
protected:
	void startScenario()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("BonewardHellfire")
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0)).heroGarrison({{CreatureID(0), 1}})
			.hero({7, 7, 0}, HeroTypeID(72), PlayerColor(1)).heroGarrison({{CreatureID(0), 1}});
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
	}

	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			GetParam() == 2 ? necromancyTest::historicalRules()
				: JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	std::pair<BattleHex, BattleHex> adjacentPositions(const battle::Unit * source)
	{
		const auto accessibility = battle()->getAccessibility();
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			const BattleHex gate(index);
			if(!gate.isAvailable() || BattleHex::getDistance(source->getPosition(), gate) > 3
				|| !accessibility.accessible(gate, false, source->unitSide()))
				continue;
			for(const auto enemy : gate.getNeighbouringTiles())
				if(enemy.isAvailable() && accessibility.accessible(enemy, false, BattleSide::DEFENDER))
					return {gate, enemy};
		}
		return {};
	}
};
}

TEST_P(NewHorizonsNecromancyHellfireTest, AcceptedArrivalAppliesBonewardToRawHundredDamage)
{
	ASSERT_NO_FATAL_FAILURE(startScenario());
	ASSERT_EQ(newHorizonsNecromancy::policy(defenderSideHero->getPerkState().rules).has_value(), GetParam() != 2);
	const auto imp = creatureByName("core:imp");
	attackerSideHero->clearSlots();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), imp, 1));
	const std::string gating = "new-horizons:demonicGating";
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(gating)),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({gating, "new-horizons:demonicGating.hellfireArrival"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(gating, "new-horizons:demonicGating.hellfireArrival"));
	defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(newHorizonsNecromancy::SKILL_ID)),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(defenderSideHero->usesNewHorizonsNecromancy());
	ASSERT_EQ(defenderSideHero->getNewHorizonsNecromancyRank(), MasteryLevel::BASIC);
	if(GetParam() != 1)
	{
		defenderSideHero->applyPerkSelection({newHorizonsNecromancy::SKILL_ID,
			newHorizonsNecromancy::DARK_CONVERSION_ID});
		ASSERT_TRUE(defenderSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
			newHorizonsNecromancy::DARK_CONVERSION_ID));
	}
	int reserve = 0;
	for(int count = 1; count <= 1000; ++count)
		if(count * imp.toCreature()->getMaxHealth() * 15 / 100 == 100)
		{
			reserve = count;
			break;
		}
	ASSERT_GT(reserve, 0) << "Fixture requires exactly 100 raw Hellfire damage";
	attackerSideHero->setDemonicReserve({{imp, reserve}});
	startBattle();
	beginCombat();
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	ASSERT_EQ(active->unitSide(), BattleSide::ATTACKER);
	const auto [destination, enemy] = adjacentPositions(active);
	ASSERT_TRUE(destination.isAvailable());
	ASSERT_TRUE(enemy.isAvailable());
	const auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:skeleton"), enemy, 200);
	ASSERT_NE(target, nullptr);
	const auto targetId = target->unitId();
	const auto health = target->getAvailableHealth();
	server.injuries.clear();
	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = imp;
	action.aimToHex(destination);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	endRound();
	const auto gated = battle()->battleGetStacksIf([](const CStack * stack)
		{ return stack->unitSlot() == SlotID::SUMMONED_SLOT_PLACEHOLDER && stack->unitSide() == BattleSide::ATTACKER; });
	ASSERT_EQ(gated.size(), 1u);
	ASSERT_EQ(gated.front()->getAvailableHealth() * 15 / 100, 100);
	const auto * after = battle()->battleGetStackByID(targetId, false);
	ASSERT_NE(after, nullptr);
	const int expected = GetParam() == 0 ? 70 : 100;
	EXPECT_EQ(health - after->getAvailableHealth(), expected);
	ASSERT_EQ(server.injuries.size(), 1u);
	ASSERT_EQ(server.injuries.front().stacks.size(), 1u);
	const auto & hit = server.injuries.front().stacks.front();
	EXPECT_EQ(hit.stackAttacked, targetId);
	EXPECT_EQ(hit.attackerID, gated.front()->unitId());
	EXPECT_EQ(hit.damageAmount, expected);
	EXPECT_TRUE(hit.flags & BattleStackAttacked::SPELL_EFFECT);
	RecordProperty("rawHellfireDamage", "100");
	RecordProperty("appliedHellfireDamage", std::to_string(expected));
}

INSTANTIATE_TEST_SUITE_P(CapturedPolicy, NewHorizonsNecromancyHellfireTest, ::testing::Values(0, 1, 2));
