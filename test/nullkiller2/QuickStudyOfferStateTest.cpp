/* Quick Study query deduplication tests, part of VCMI / New Horizons; GPL v2.0 or later. */
#include "StdInc.h"
#include "AI/Nullkiller2/Helpers/LevelUpOfferState.h"

namespace
{
using State = NK2AI::LevelUpOfferState;
using Event = State::Event;
const ObjectInstanceID HERO(3), OTHER(4);
const QueryID FIRST(20), SECOND(21);
}

TEST(QuickStudyOfferStateTest, NoCommandBeforeObservedOffer)
{
	State state;
	EXPECT_FALSE(state.reroll(HERO, FIRST));
	EXPECT_FALSE(state.select(HERO, FIRST));
	EXPECT_EQ(state.observe(HERO, FIRST, QueryID::NONE, true), Event::INITIAL);
	EXPECT_TRUE(state.reroll(HERO, FIRST));
	EXPECT_FALSE(state.reroll(HERO, FIRST));
	EXPECT_FALSE(state.select(HERO, FIRST));
}

TEST(QuickStudyOfferStateTest, FailedAckAloneCannotAuthorizeFallback)
{
	State state;
	ASSERT_EQ(state.observe(HERO, FIRST, QueryID::NONE, true), Event::INITIAL);
	ASSERT_TRUE(state.reroll(HERO, FIRST));
	// There is deliberately no ACK-to-fallback API. An unchanged initial packet is not confirmation.
	EXPECT_EQ(state.observe(HERO, FIRST, QueryID::NONE, true), Event::IGNORE);
	EXPECT_FALSE(state.select(HERO, FIRST));
}

TEST(QuickStudyOfferStateTest, SameIdConfirmationKeepsOriginalExactlyOnceEvenIfRerollStillAvailable)
{
	State state;
	ASSERT_EQ(state.observe(HERO, FIRST, QueryID::NONE, true), Event::INITIAL);
	ASSERT_TRUE(state.reroll(HERO, FIRST));
	EXPECT_EQ(state.observe(HERO, FIRST, FIRST, true), Event::CONFIRMED_REJECTION);
	EXPECT_FALSE(state.reroll(HERO, FIRST));
	EXPECT_TRUE(state.select(HERO, FIRST));
	EXPECT_FALSE(state.select(HERO, FIRST));
	EXPECT_EQ(state.observe(HERO, FIRST, FIRST, true), Event::IGNORE);
}

TEST(QuickStudyOfferStateTest, FreshReplacementAnswersOnceWithoutSecondReroll)
{
	State state;
	ASSERT_EQ(state.observe(HERO, FIRST, QueryID::NONE, true), Event::INITIAL);
	ASSERT_TRUE(state.reroll(HERO, FIRST));
	EXPECT_EQ(state.observe(HERO, SECOND, FIRST, false), Event::REPLACEMENT);
	EXPECT_FALSE(state.reroll(HERO, SECOND));
	EXPECT_EQ(state.observe(HERO, FIRST, FIRST, true), Event::IGNORE);
	EXPECT_FALSE(state.select(HERO, FIRST));
	EXPECT_TRUE(state.select(HERO, SECOND));
	EXPECT_EQ(state.observe(HERO, SECOND, FIRST, false), Event::IGNORE);
	EXPECT_FALSE(state.select(HERO, SECOND));
}

TEST(QuickStudyOfferStateTest, UnexpectedReplacementDoesNotReplaceAnUnrequestedOffer)
{
	State state;
	EXPECT_EQ(state.observe(HERO, SECOND, FIRST, false), Event::IGNORE);
	ASSERT_EQ(state.observe(HERO, FIRST, QueryID::NONE, true), Event::INITIAL);
	EXPECT_EQ(state.observe(HERO, SECOND, FIRST, false), Event::IGNORE);
	EXPECT_TRUE(state.select(HERO, FIRST));
}

TEST(QuickStudyOfferStateTest, OrdinaryChoiceAndDuplicateCallbacksCannotRequestReroll)
{
	State state;
	ASSERT_EQ(state.observe(HERO, FIRST, QueryID::NONE, false), Event::INITIAL);
	EXPECT_FALSE(state.reroll(HERO, FIRST));
	EXPECT_TRUE(state.select(HERO, FIRST));
	EXPECT_EQ(state.observe(HERO, FIRST, QueryID::NONE, false), Event::IGNORE);
	EXPECT_EQ(state.observe(HERO, FIRST, FIRST, true), Event::IGNORE);
	EXPECT_FALSE(state.select(HERO, FIRST));
}

TEST(QuickStudyOfferStateTest, NewOrdinaryLevelHasItsOwnOpportunity)
{
	State state;
	ASSERT_EQ(state.observe(HERO, FIRST, QueryID::NONE, true), Event::INITIAL);
	ASSERT_TRUE(state.reroll(HERO, FIRST));
	ASSERT_EQ(state.observe(HERO, FIRST, FIRST, true), Event::CONFIRMED_REJECTION);
	ASSERT_TRUE(state.select(HERO, FIRST));
	EXPECT_EQ(state.observe(HERO, SECOND, QueryID::NONE, true), Event::INITIAL);
	EXPECT_TRUE(state.reroll(HERO, SECOND));
}

TEST(QuickStudyOfferStateTest, HeroQueriesRemainIndependentAndInvalidIdsAreIgnored)
{
	State state;
	EXPECT_EQ(state.observe(ObjectInstanceID::NONE, FIRST, QueryID::NONE, true), Event::IGNORE);
	EXPECT_EQ(state.observe(HERO, QueryID::NONE, QueryID::NONE, true), Event::IGNORE);
	ASSERT_EQ(state.observe(HERO, FIRST, QueryID::NONE, true), Event::INITIAL);
	ASSERT_EQ(state.observe(OTHER, SECOND, QueryID::NONE, true), Event::INITIAL);
	EXPECT_TRUE(state.reroll(HERO, FIRST));
	EXPECT_TRUE(state.select(OTHER, SECOND));
	EXPECT_EQ(state.observe(HERO, FIRST, FIRST, true), Event::CONFIRMED_REJECTION);
	EXPECT_TRUE(state.select(HERO, FIRST));
}
