/*
 * OverwhelmingFormulaStateTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include <array>

#include "../../lib/battle/OverwhelmingFormulaState.h"

namespace
{
struct FormulaStateHandler
{
	bool saving;
	std::array<uint32_t, 4> fields{};
	size_t position = 0;

	void operator&(uint32_t & field)
	{
		if(saving)
			fields.at(position++) = field;
		else
			field = fields.at(position++);
	}
};
}

TEST(OverwhelmingFormulaState, AcceptedCastsAndPredictionsDoNotConsumeFirstUse)
{
	OverwhelmingFormulaState state;
	EXPECT_FALSE(state.hasState());
	const auto first = state.registerAcceptedEligibleCast();
	const auto second = state.registerAcceptedEligibleCast();
	EXPECT_EQ(first, 1u);
	EXPECT_EQ(second, 2u);
	EXPECT_TRUE(state.canPenetrate(first));
	EXPECT_TRUE(state.canPenetrate(second));
	EXPECT_EQ(state.winningCastToken, OverwhelmingFormulaState::INVALID_CAST_TOKEN);
}

TEST(OverwhelmingFormulaState, NonqualifyingDamageDoesNotConsumeFirstUse)
{
	OverwhelmingFormulaState state;
	const auto token = state.registerAcceptedEligibleCast();
	const auto acceptedState = state;
	EXPECT_FALSE(state.claimActualDamage(token, false, 10, true));
	EXPECT_FALSE(state.claimActualDamage(token, true, 0, true));
	EXPECT_FALSE(state.claimActualDamage(token, true, -1, true));
	EXPECT_FALSE(state.claimActualDamage(token, true, 10, false));
	EXPECT_EQ(state, acceptedState);
	EXPECT_TRUE(state.claimActualDamage(token, true, 1, true));
}

TEST(OverwhelmingFormulaState, FirstActualDamageSelectsCastRatherThanAcceptanceOrder)
{
	OverwhelmingFormulaState state;
	const auto delayed = state.registerAcceptedEligibleCast();
	const auto immediate = state.registerAcceptedEligibleCast();
	ASSERT_TRUE(state.claimActualDamage(immediate, true, 10, true));
	EXPECT_EQ(state.winningCastToken, immediate);
	EXPECT_FALSE(state.canPenetrate(delayed));
	EXPECT_FALSE(state.claimActualDamage(delayed, true, 20, true));
	EXPECT_EQ(state.winningCastToken, immediate);
}

TEST(OverwhelmingFormulaState, WinningCastKeepsAllHitsTargetsAndLaterMarkers)
{
	OverwhelmingFormulaState state;
	const auto winner = state.registerAcceptedEligibleCast();
	ASSERT_TRUE(state.claimActualDamage(winner, true, 10, true));
	const auto laterCast = state.registerAcceptedEligibleCast();
	EXPECT_TRUE(state.canPenetrate(winner));
	EXPECT_TRUE(state.claimActualDamage(winner, true, 5, true));
	EXPECT_FALSE(state.claimActualDamage(winner, true, 0, true));
	EXPECT_TRUE(state.canPenetrate(winner));
	EXPECT_FALSE(state.canPenetrate(laterCast));
	EXPECT_FALSE(state.claimActualDamage(laterCast, true, 10, true));
	EXPECT_TRUE(state.claimActualDamage(winner, true, 15, true));
	EXPECT_EQ(state.winningCastToken, winner);
}

TEST(OverwhelmingFormulaState, MissingAndUnknownCastTokensAreInert)
{
	OverwhelmingFormulaState state;
	EXPECT_FALSE(state.canPenetrate(0));
	EXPECT_FALSE(state.canPenetrate(1));
	EXPECT_FALSE(state.claimActualDamage(0, true, 10, true));
	EXPECT_FALSE(state.claimActualDamage(1, true, 10, true));
	const auto token = state.registerAcceptedEligibleCast();
	EXPECT_FALSE(state.canPenetrate(token + 1));
	EXPECT_FALSE(state.claimActualDamage(token + 1, true, 10, true));
	EXPECT_EQ(state.winningCastToken, OverwhelmingFormulaState::INVALID_CAST_TOKEN);
}

TEST(OverwhelmingFormulaState, TokenOverflowIsRejectedWithoutWrappingOrChangingWinner)
{
	OverwhelmingFormulaState state;
	state.lastCandidateCastToken = std::numeric_limits<OverwhelmingFormulaState::CastToken>::max() - 1;
	const auto finalToken = state.registerAcceptedEligibleCast();
	EXPECT_EQ(finalToken, std::numeric_limits<OverwhelmingFormulaState::CastToken>::max());
	ASSERT_TRUE(state.claimActualDamage(finalToken, true, 10, true));
	const auto before = state;
	EXPECT_EQ(state.registerAcceptedEligibleCast(), OverwhelmingFormulaState::INVALID_CAST_TOKEN);
	EXPECT_EQ(state, before);
}

TEST(OverwhelmingFormulaState, SavedStatePreservesDelayedEntitlementAndResetStartsNewCombat)
{
	OverwhelmingFormulaState original;
	const auto delayed = original.registerAcceptedEligibleCast();
	const auto winner = original.registerAcceptedEligibleCast();
	ASSERT_TRUE(original.claimActualDamage(winner, true, 10, true));
	FormulaStateHandler writer{true};
	original.serialize(writer);
	EXPECT_EQ(writer.position, 4u);

	OverwhelmingFormulaState restored;
	FormulaStateHandler reader{false, writer.fields};
	restored.serialize(reader);
	EXPECT_EQ(restored, original);
	EXPECT_FALSE(restored.canPenetrate(delayed));
	EXPECT_TRUE(restored.canPenetrate(winner));
	EXPECT_TRUE(restored.claimActualDamage(winner, true, 10, true));

	restored.reset();
	EXPECT_EQ(restored, OverwhelmingFormulaState{});
	EXPECT_EQ(restored.registerAcceptedEligibleCast(), 1u);
}

TEST(OverwhelmingFormulaState, PendingSavedCastsStillCompeteAtActualResolution)
{
	OverwhelmingFormulaState original;
	const auto first = original.registerAcceptedEligibleCast();
	const auto second = original.registerAcceptedEligibleCast();
	FormulaStateHandler writer{true};
	original.serialize(writer);

	OverwhelmingFormulaState restored;
	FormulaStateHandler reader{false, writer.fields};
	restored.serialize(reader);
	EXPECT_EQ(restored, original);
	EXPECT_TRUE(restored.canPenetrate(first));
	EXPECT_TRUE(restored.canPenetrate(second));
	ASSERT_TRUE(restored.claimActualDamage(second, true, 1, true));
	EXPECT_FALSE(restored.canPenetrate(first));
}

TEST(OverwhelmingFormulaState, InvalidSavedWinnerIsRejectedOnSaveAndLoad)
{
	OverwhelmingFormulaState invalid;
	invalid.winningCastToken = 1;
	const auto before = invalid;
	EXPECT_FALSE(invalid.canPenetrate(1));
	EXPECT_FALSE(invalid.claimActualDamage(1, true, 10, true));
	EXPECT_EQ(invalid.registerAcceptedEligibleCast(), OverwhelmingFormulaState::INVALID_CAST_TOKEN);
	EXPECT_EQ(invalid, before);
	EXPECT_THROW(invalid.validateShape(), std::runtime_error);
	FormulaStateHandler writer{true};
	EXPECT_THROW(invalid.serialize(writer), std::runtime_error);
	EXPECT_EQ(writer.position, 0u);

	OverwhelmingFormulaState restored;
	FormulaStateHandler reader{false, {0, 1, 0, 2}};
	EXPECT_THROW(restored.serialize(reader), std::runtime_error);
}
