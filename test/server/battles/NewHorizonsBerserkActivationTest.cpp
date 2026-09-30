/*
 * NewHorizonsBerserkActivationTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

#include <vstd/RNG.h>

class NewHorizonsBerserkActivationTest : public HeroCommandFixture
{
protected:
	CStack * berserker = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void prepare(const BattleHex & position)
	{
		startGame();
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		berserker = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), position, 10);
		ASSERT_NE(berserker, nullptr);
		berserker->addNewBonus(std::make_shared<Bonus>(BonusDuration::UNTIL_OWN_ATTACK,
			BonusType::ATTACKS_NEAREST_CREATURE, BonusSource::SPELL_EFFECT, 0, BonusSourceID(SpellID(SpellID::BERSERK))));
	}
};

TEST_F(NewHorizonsBerserkActivationTest, AuthoritativeShooterUsesSeededMeleeTieRatherThanShooting)
{
	ASSERT_NO_FATAL_FAILURE(prepare(BattleHex(8, 5)));
	addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(7, 5), 100);
	addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(9, 5), 100);
	const auto candidates = battle()->getBerserkForcedActions(berserker);
	ASSERT_EQ(candidates.size(), 2u);
	for(const auto & candidate : candidates)
		ASSERT_EQ(candidate.type, EActionType::WALK_AND_ATTACK);

	CRandomGenerator expectedRandom(BattleTestFixture::seed);
	const auto expectedTarget = RandomGeneratorUtil::nextItem(candidates, expectedRandom)->target->unitId();
	gameHandler->randomizer->setSeed(BattleTestFixture::seed);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	const auto started = std::ranges::find_if(server.startedActions, [&](const StartAction & pack)
	{
		return pack.ba.stackNumber == berserker->unitId();
	});
	ASSERT_NE(started, server.startedActions.end());
	EXPECT_EQ(started->ba.actionType, EActionType::WALK_AND_ATTACK);
	EXPECT_EQ(started->ba.side, BattleSide::DEFENDER);
	ASSERT_FALSE(server.attacks.empty());
	ASSERT_FALSE(server.attacks.front().bsa.empty());
	EXPECT_EQ(server.attacks.front().bsa.front().stackAttacked, expectedTarget);
}

TEST_F(NewHorizonsBerserkActivationTest, AuthoritativeDefenderWalkUsesItsOwnSideAndSharedDestination)
{
	ASSERT_NO_FATAL_FAILURE(prepare(BattleHex(12, 5)));
	addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(2, 5), 100);
	const auto candidates = battle()->getBerserkForcedActions(berserker);
	ASSERT_EQ(candidates.size(), 1u);
	ASSERT_EQ(candidates.front().type, EActionType::WALK);
	const auto expectedPosition = candidates.front().position;
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	const auto started = std::ranges::find_if(server.startedActions, [&](const StartAction & pack)
	{
		return pack.ba.stackNumber == berserker->unitId();
	});
	ASSERT_NE(started, server.startedActions.end());
	EXPECT_EQ(started->ba.actionType, EActionType::WALK);
	EXPECT_EQ(started->ba.side, BattleSide::DEFENDER);
	EXPECT_EQ(berserker->getPosition(), expectedPosition);
}
