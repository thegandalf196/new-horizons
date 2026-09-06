/*
 * HeroCommandTurnLifecycleTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/networkPacks/SetStackEffect.h"

class HeroCommandTurnLifecycleTest : public HeroCommandFixture
{
protected:
	void addUntilTurnBonus()
	{
		Bonus bonus;
		bonus.type = BonusType::STACKS_SPEED;
		bonus.val = 5;
		bonus.duration = BonusDuration::STACK_GETS_TURN;
		SetStackEffect effect;
		effect.battleID = BattleID(0);
		effect.toAdd.emplace_back(battle()->battleActiveUnit()->unitId(), std::vector<Bonus>{bonus});
		gameHandler->sendAndApply(effect);
		ASSERT_FALSE(battle()->battleActiveUnit()->getAllBonuses(Bonus::UntilGetsTurn)->empty());
	}
};

TEST_F(HeroCommandTurnLifecycleTest, HeroSpellKeepsItsExistingExpirySemantics)
{
	prepareCommands(true);
	addUntilTurnBonus();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), heroAction(0)));
	EXPECT_TRUE(battle()->battleActiveUnit()->getAllBonuses(Bonus::UntilGetsTurn)->empty());
}

TEST_F(HeroCommandTurnLifecycleTest, PacketControlsDistinguishNoTurnSpellFromFreshUnitTurn)
{
	prepareCommands();
	addUntilTurnBonus();
	const auto * active = battle()->battleActiveUnit();
	const auto speed = active->getMovementRange();
	// Authoritative packet/state characterization, not a fabricated creature
	// spell journey. The existing no-turn exception must remain intact.
	BattleSetActiveStack packet;
	packet.battleID = BattleID(0);
	packet.stack = active->unitId();
	packet.reason = BattleUnitTurnReason::UNIT_SPELLCAST;
	gameHandler->sendAndApply(packet);
	EXPECT_EQ(active->getMovementRange(), speed);
	EXPECT_FALSE(active->getAllBonuses(Bonus::UntilGetsTurn)->empty());

	packet.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(packet);
	EXPECT_TRUE(active->getAllBonuses(Bonus::UntilGetsTurn)->empty());
	EXPECT_LT(active->getMovementRange(), speed);
}
