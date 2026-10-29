/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/NewHorizonsForestAmbushState.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/serializer/CMemorySerializer.h"
using battle::ForestAmbushState;

TEST(NewHorizonsForestAmbushStateTest, WaitArmsOnePhysicalRangedStrikeAndOutgoingPhysicalAttacksReveal)
{
	ForestAmbushState state;
	state.acceptWait(true);
	EXPECT_TRUE(state.concealed);
	EXPECT_FALSE(state.shotPending);
	state.finishActivation(); // Wait ends its action, not the delayed activation.
	EXPECT_TRUE(state.concealed);
	state.beginActivation(true);
	EXPECT_FALSE(state.concealed);
	EXPECT_TRUE(state.shotPending);
	state.finishStrike(true, false); // A nonphysical cast/attack is not this reward.
	EXPECT_TRUE(state.shotPending);
	state.finishStrike(true, true); // Any eligible primary ranged kind consumes it.
	EXPECT_FALSE(state.shotPending);
	state.acceptWait(true);
	EXPECT_FALSE(state.concealed) << "a repeated Wait cannot rearm the round";
	for(const bool ranged : {false, true})
	{
		ForestAmbushState reaction;
		reaction.acceptWait(true);
		reaction.finishStrike(ranged, true);
		EXPECT_FALSE(reaction.concealed);
		reaction.beginActivation(true);
		EXPECT_FALSE(reaction.shotPending) << "an earlier physical reaction revealed it";
	}
}

TEST(NewHorizonsForestAmbushStateTest, IncomingAttackBeforeWaitOrWhileHiddenPreventsTheReward)
{
	ForestAmbushState prior;
	prior.receiveAttack(); // Zero damage is still an admitted attack event.
	prior.acceptWait(true);
	EXPECT_FALSE(prior.concealed);
	prior.beginActivation(true);
	EXPECT_FALSE(prior.shotPending);
	ForestAmbushState hidden;
	hidden.acceptWait(true);
	hidden.receiveAttack();
	EXPECT_FALSE(hidden.concealed);
	hidden.beginActivation(true);
	EXPECT_FALSE(hidden.shotPending);
}

TEST(NewHorizonsForestAmbushStateTest, EarnedShotDoesNotExtendAcrossActivationsOrRearmFromExtraTurns)
{
	ForestAmbushState state;
	state.acceptWait(true);
	state.beginActivation(true);
	state.receiveAttack(); // A reaction after reveal cannot undo the earned shot.
	EXPECT_TRUE(state.shotPending);
	state.finishActivation();
	EXPECT_FALSE(state.shotPending);
	state.beginActivation(false);
	state.acceptWait(true);
	EXPECT_FALSE(state.concealed);
	EXPECT_FALSE(state.shotPending);
}

TEST(NewHorizonsForestAmbushStateTest, WrongEntitlementDeathAndNonDelayedActivationDoNotKeepConcealment)
{
	ForestAmbushState disabled;
	disabled.acceptWait(false);
	EXPECT_FALSE(disabled.concealed);
	ForestAmbushState state;
	state.acceptWait(true);
	state.beginActivation(false);
	EXPECT_FALSE(state.concealed);
	EXPECT_FALSE(state.shotPending);
	state = {};
	state.acceptWait(true);
	state.retire();
	EXPECT_FALSE(state.concealed);
	state.acceptWait(true);
	EXPECT_FALSE(state.concealed) << "resurrection is not a new round";
}

TEST(NewHorizonsForestAmbushStateTest, StrictJsonRoundTripAndAbsentStateReset)
{
	ForestAmbushState state;
	state.acceptWait(true);
	EXPECT_EQ(ForestAmbushState::fromJson(state.toJson()), state);
	state.beginActivation(true);
	EXPECT_EQ(ForestAmbushState::fromJson(state.toJson()), state);
	EXPECT_EQ(ForestAmbushState::fromJson(JsonNode()), ForestAmbushState{});
	for(const auto text : {R"({"concealed":true})", R"({"waited":true,"concealed":true,"attacked":true})",
		R"({"waited":true,"concealed":true,"shotPending":true})", R"({"waited":1})", R"({"future":false})"})
	{
		const JsonNode malformed(text, std::string_view(text).size(), "Forest Ambush test");
		EXPECT_THROW(ForestAmbushState::fromJson(malformed), std::runtime_error);
	}
}

TEST(NewHorizonsForestAmbushStateTest, BinaryRoundTripRejectsUnsupportedNonemptyBeforePayload)
{
	ForestAmbushState state;
	state.acceptWait(true);
	CMemorySerializer bytes;
	ASSERT_NO_THROW(bytes.oser & state);
	ForestAmbushState restored;
	ASSERT_NO_THROW(bytes.iser & restored);
	EXPECT_EQ(restored, state);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_RECRUITMENT_GROWTH;
	EXPECT_THROW(old.oser & state, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	ForestAmbushState absent;
	CMemorySerializer legacy;
	legacy.oser.version = old.oser.version;
	legacy.iser.version = old.oser.version;
	ASSERT_NO_THROW(legacy.oser & absent);
	ASSERT_NO_THROW(legacy.iser & restored);
	EXPECT_EQ(restored, ForestAmbushState{});
}
