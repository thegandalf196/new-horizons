/*
 * HeroOrderLogTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../server/battles/HeroOrderLog.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/texts/MetaString.h"

TEST(HeroOrderLogNames, CurrentAndHistoricalOrderNamesRemainDistinct)
{
	for(const auto & [command, name] : std::vector<std::pair<HeroCommand, const char *>>{
		{HeroCommand::CHARGE, "Charge"}, {HeroCommand::HOLD_THE_LINE, "Hold the Line"},
		{HeroCommand::FOCUS_FIRE, "Focus Fire"}, {HeroCommand::RIPOSTE, "Riposte"},
		{HeroCommand::BRACE, "Brace"}, {HeroCommand::PROTECT, "Protect"},
		{HeroCommand::FLANK, "Flank"}, {HeroCommand::SECOND_WIND, "Second Wind"},
		{HeroCommand::FEINT, "Feint"}, {HeroCommand::VANGUARD, "Vanguard"}})
		EXPECT_STREQ(heroOrderDisplayName(command), name);
}

class HeroOrderLogTest : public HeroCommandFixture
{
protected:
	void prepare()
	{
		useRevisedOrders = true;
		prepareCommands();
		server.battleLogLines.clear();
	}
};

TEST_F(HeroOrderLogTest, AcceptedFeintLogReadsFinalizedReduction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(issue(HeroCommand::FEINT));
	const auto state = battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::FEINT);
	ASSERT_TRUE(state && state->revision && state->revision->potencyFinalized);
	ASSERT_EQ(server.battleLogLines.size(), 1);
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr("Feint!"));
	EXPECT_THAT(server.battleLogLines.front(), ::testing::HasSubstr(
		std::to_string(state->revision->feintReductionPercent) + "% less physical retaliation damage"));
}

TEST_F(HeroOrderLogTest, AcceptedVanguardLogReadsFinalizedStatsAndInclusiveDuration)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(issue(HeroCommand::VANGUARD));
	const auto state = battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::VANGUARD);
	ASSERT_TRUE(state && state->revision && state->revision->potencyFinalized);
	ASSERT_EQ(server.battleLogLines.size(), 1);
	const auto & snapshot = *state->revision;
	const auto & log = server.battleLogLines.front();
	EXPECT_THAT(log, ::testing::HasSubstr("Vanguard!"));
	EXPECT_THAT(log, ::testing::HasSubstr("+" + std::to_string(snapshot.vanguardStatBonus) + " Creature Attack, Defense and Morale"));
	EXPECT_THAT(log, ::testing::HasSubstr("+" + std::to_string(snapshot.vanguardInitiativePercent) + "% Initiative"));
	EXPECT_THAT(log, ::testing::HasSubstr("for " + std::to_string(snapshot.expiresAfterRound - state->issuedRound + 1) + " round"));
	EXPECT_THAT(log, ::testing::HasSubstr("through round " + std::to_string(snapshot.expiresAfterRound)));
}

TEST_F(HeroOrderLogTest, CrownFeedbackReadsStoredVariantsWithoutRecalculatingHero)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	HeroOrderState state;
	state.command = HeroCommand::VANGUARD;
	state.issuedRound = 4;
	state.revision.emplace();
	state.revision->potencyFinalized = true;
	state.revision->expiresAfterRound = 6;
	state.revision->vanguardStatBonus = 2;
	state.revision->vanguardInitiativePercent = 12;
	state.revision->recipientUnitIds = {battle()->battleActiveUnit()->unitId()};
	state.crownAndAltarRecipientUnitIds = state.revision->recipientUnitIds;
	state.revision->crown = heroCommands::CrownRevisedOrderPotency{0, 3, 13};
	ASSERT_NO_THROW(state.validateShape());
	const auto log = heroOrderLogLine(*battle(), BattleSide::ATTACKER, state).toString(LIBRARY->staticTexts());
	EXPECT_THAT(log, ::testing::HasSubstr("+2 Creature Attack, Defense and Morale, and +12% Initiative"));
	EXPECT_THAT(log, ::testing::HasSubstr("3 rounds, including this round (through round 6)"));
	EXPECT_THAT(log, ::testing::HasSubstr("Crown and Altar recipients instead gain +3 Creature Attack, Defense and Morale, and +13% Initiative"));
	state.command = HeroCommand::FEINT;
	state.revision->recipientUnitIds.clear();
	state.revision->expiresAfterRound = state.issuedRound;
	state.revision->vanguardStatBonus = 0;
	state.revision->vanguardInitiativePercent = 0;
	state.revision->feintReductionPercent = 30;
	state.revision->crown = heroCommands::CrownRevisedOrderPotency{33, 0, 0};
	ASSERT_NO_THROW(state.validateShape());
	const auto feint = heroOrderLogLine(*battle(), BattleSide::ATTACKER, state).toString(LIBRARY->staticTexts());
	EXPECT_THAT(feint, ::testing::HasSubstr("allied stacks take 30% less physical retaliation damage"));
	EXPECT_THAT(feint, ::testing::HasSubstr("Crown and Altar recipients take 33% less physical retaliation damage"));
}
