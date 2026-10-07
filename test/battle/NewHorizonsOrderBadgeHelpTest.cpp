/*
 * NewHorizonsOrderBadgeHelpTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt file in main folder
 */
#include "../StdInc.h"

#include "../../client/windows/NewHorizonsOrderBadgeHelp.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../server/battles/HeroCommandFixture.h"

class NewHorizonsOrderBadgeHelpTest : public HeroCommandFixture {};

TEST_F(NewHorizonsOrderBadgeHelpTest, FormatsTranslatedIssuingSideAndOrderRound)
{
	startGame();

	HeroOrderState attackerOrder;
	attackerOrder.command = HeroCommand::PROTECT;
	attackerOrder.issuedRound = 3;
	const auto attackerOriginal = attackerOrder;
	const auto attackerHelp = newHorizonsOrderBadgeHelp::sourceAndExpiry(
		attackerOrder, BattleSide::ATTACKER, 3);
	EXPECT_EQ(attackerHelp.toString(LIBRARY->generaltexth.get()),
		"Issued by the attacking hero. Expires at the end of round 3; its benefit may end earlier when used or broken.");
	EXPECT_EQ(attackerOrder, attackerOriginal);

	HeroOrderState defenderOrder;
	defenderOrder.command = HeroCommand::BRACE;
	defenderOrder.issuedRound = 11;
	const auto defenderOriginal = defenderOrder;
	const auto defenderHelp = newHorizonsOrderBadgeHelp::sourceAndExpiry(
		defenderOrder, BattleSide::DEFENDER, 11);
	EXPECT_EQ(defenderHelp.toString(LIBRARY->generaltexth.get()),
		"Issued by the defending hero. Expires at the end of round 11; its benefit may end earlier when used or broken.");
	EXPECT_EQ(defenderOrder, defenderOriginal);
}

TEST_F(NewHorizonsOrderBadgeHelpTest, InvalidOrNoncurrentOrderMetadataProducesNoHelp)
{
	startGame();
	HeroOrderState state;
	state.command = HeroCommand::PROTECT;
	state.issuedRound = 4;
	const auto original = state;

	EXPECT_TRUE(newHorizonsOrderBadgeHelp::sourceAndExpiry(state, BattleSide::ATTACKER, 5).empty())
		<< "An expired issued round must not produce a badge note.";
	EXPECT_TRUE(newHorizonsOrderBadgeHelp::sourceAndExpiry(state, BattleSide::ATTACKER, 3).empty())
		<< "Future/noncurrent metadata must not be shown as a current Order.";
	EXPECT_TRUE(newHorizonsOrderBadgeHelp::sourceAndExpiry(state, BattleSide::NONE, 4).empty());

	state.command = HeroCommand::NONE;
	EXPECT_TRUE(newHorizonsOrderBadgeHelp::sourceAndExpiry(state, BattleSide::ATTACKER, 4).empty());
	state.command = HeroCommand::PROTECT;
	state.issuedRound = 0;
	EXPECT_TRUE(newHorizonsOrderBadgeHelp::sourceAndExpiry(state, BattleSide::ATTACKER, 0).empty());
	state.issuedRound = -1;
	EXPECT_TRUE(newHorizonsOrderBadgeHelp::sourceAndExpiry(state, BattleSide::DEFENDER, -1).empty());
	EXPECT_EQ(state.issuedRound, -1);

	EXPECT_EQ(original.command, HeroCommand::PROTECT);
	EXPECT_EQ(original.issuedRound, 4);
}
