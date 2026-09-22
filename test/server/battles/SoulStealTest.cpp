/*
 * SoulStealTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "BattleTestFixture.h"
#include "../../../server/CGameHandler.h"

#include "../../../lib/spells/NewHorizonsSorcery.h"

namespace
{

constexpr int32_t stealerCount = 10;
constexpr int32_t victimCount = 5000;
/// Creatures the fixture's stealer gains for every enemy creature it kills.
constexpr int32_t gainPerKill = 2;

}

/// Soul steal raises its bearer's stack for every enemy creature it killed, past the size the
/// stack started at. Only the living leave souls behind.
class SoulStealTest : public BattleTestFixture
{
public:
	CStack * addPhantomVictim()
	{
		battle::UnitInfo info;
		info.id = battle()->battleNextUnitId();
		info.count = victimCount;
		info.type = creatureByName("core:pikeman");
		info.side = BattleSide::ATTACKER;
		info.position = BattleHex(leftHex);
		info.summoned = true;
		info.phantomIntegrity = 1;
		info.phantomDuration = newHorizonsSorcery::PHANTOM_ARMY_DURATION_ROUNDS;

		BattleUnitsChanged pack;
		pack.battleID = BattleID(0);
		pack.changedStacks.emplace_back(info.id, UnitChanges::EOperation::ADD);
		info.save(pack.changedStacks.back().data);
		gameHandler->sendAndApply(pack);
		return battle()->getStack(info.id);
	}

	void setUpBattle(const std::string & victimCreature)
	{
		startGame();
		startBattle();

		victim = addStack(BattleSide::ATTACKER, creatureByName(victimCreature), BattleHex(leftHex), victimCount);
		stealer = addStack(BattleSide::DEFENDER, creatureByName("vcmi-test:testSoulStealer"), BattleHex(rightHex), stealerCount);
		ASSERT_NE(victim, nullptr);
		ASSERT_NE(stealer, nullptr);

		blockRetaliation(stealer);
	}

	CStack * victim = nullptr;
	CStack * stealer = nullptr;
};

TEST_F(SoulStealTest, GrowsPastItsOriginalSizeWithEveryKill)
{
	setUpBattle("core:pikeman");

	const int32_t victimCountBefore = victim->getCount();

	ASSERT_TRUE(attack(stealer, BattleHex(leftHex)));

	const int32_t killed = victimCountBefore - victim->getCount();
	ASSERT_GT(killed, 0);

	EXPECT_EQ(stealer->getCount(), stealerCount + killed * gainPerKill);
}

TEST_F(SoulStealTest, TakesNoSoulsFromTheUndead)
{
	setUpBattle("core:skeleton");

	ASSERT_TRUE(attack(stealer, BattleHex(leftHex)));

	ASSERT_LT(victim->getCount(), victimCount) << "the attack was meant to kill some skeletons";
	EXPECT_EQ(stealer->getCount(), stealerCount);
}

TEST_F(SoulStealTest, TakesNoSoulsFromPhantomArmy)
{
	startGame();
	startBattle();
	victim = addPhantomVictim();
	stealer = addStack(BattleSide::DEFENDER, creatureByName("vcmi-test:testSoulStealer"), BattleHex(rightHex), stealerCount);
	ASSERT_NE(victim, nullptr);
	ASSERT_NE(stealer, nullptr);
	blockRetaliation(stealer);
	forceMaximumDamage(stealer);

	ASSERT_TRUE(attack(stealer, BattleHex(leftHex)));

	EXPECT_FALSE(victim->alive()) << "the attack should kill the Phantom stack's remaining Integrity";
	EXPECT_EQ(stealer->getCount(), stealerCount);
}

TEST_F(SoulStealTest, KillingNobodyGainsNothing)
{
	// one black dragon has more health than the whole attacking stack can deal in one blow
	setUpBattle("core:blackDragon");

	ASSERT_TRUE(attack(stealer, BattleHex(leftHex)));

	ASSERT_EQ(victim->getCount(), victimCount);
	EXPECT_EQ(stealer->getCount(), stealerCount);
}
