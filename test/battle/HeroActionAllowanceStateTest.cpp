/*
 * HeroActionAllowanceStateTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../../lib/battle/HeroActionAllowanceState.h"
#include "../../lib/serializer/CMemorySerializer.h"

#include <limits>

namespace
{
using Ledger = HeroActionAllowanceState;
using Action = Ledger::ActionKind;
using Allowance = Ledger::AllowanceKind;
using Source = Ledger::GrantSource;

}

TEST(HeroActionAllowanceState, OneFlexibleHeroAllowancePaysEitherKindOnce)
{
	Ledger ledger;
	EXPECT_THROW(ledger.eligibleAllowance(Action::SPELL, 0), std::invalid_argument);
	EXPECT_THROW(ledger.remainingCounts(0), std::invalid_argument);

	ledger.resetForRound(0);
	const auto first = ledger.eligibleAllowance(Action::SPELL, 0);
	ASSERT_TRUE(first);
	EXPECT_EQ(first->allowance, Allowance::HERO);
	EXPECT_EQ(first->source, Source::ROUND);

	const auto spellReceipt = ledger.consumeAllowance(first->grantId, Action::SPELL, 0);
	ASSERT_TRUE(spellReceipt);
	EXPECT_EQ(spellReceipt->action, Action::SPELL);
	EXPECT_EQ(spellReceipt->allowance, Allowance::HERO);
	EXPECT_EQ(spellReceipt->source, Source::ROUND);
	EXPECT_FALSE(ledger.eligibleAllowance(Action::ORDER, 0));
	EXPECT_EQ(ledger.remainingCounts(0), (Ledger::Counts{}));

	Ledger orderLedger;
	orderLedger.resetForRound(0);
	const auto order = orderLedger.eligibleAllowance(Action::ORDER, 0);
	ASSERT_TRUE(order);
	const auto orderReceipt = orderLedger.consumeAllowance(order->grantId, Action::ORDER, 0);
	ASSERT_TRUE(orderReceipt);
	EXPECT_EQ(orderReceipt->action, Action::ORDER);
	EXPECT_EQ(orderReceipt->allowance, Allowance::HERO);
	EXPECT_FALSE(orderLedger.eligibleAllowance(Action::SPELL, 0));
}

TEST(HeroActionAllowanceState, SpecializedGrantWinsBeforeFlexibleHeroAllowance)
{
	Ledger ledger;
	ledger.resetForRound(1);
	const auto hero = ledger.eligibleAllowance(Action::SPELL, 1);
	ASSERT_TRUE(hero);
	const auto laterSpell = ledger.grantAllowance(Allowance::SPELL, Source::METAMAGIC, 3);
	const auto earlierSpell = ledger.grantAllowance(Allowance::SPELL, Source::PERK, 2);

	const auto selected = ledger.eligibleAllowance(Action::SPELL, 1);
	ASSERT_TRUE(selected);
	EXPECT_EQ(selected->grantId, earlierSpell);
	EXPECT_EQ(selected->allowance, Allowance::SPELL);
	EXPECT_EQ(selected->source, Source::PERK);

	const auto receipt = ledger.consumeAllowance(selected->grantId, Action::SPELL, 1);
	ASSERT_TRUE(receipt);
	EXPECT_EQ(receipt->grantId, earlierSpell);
	EXPECT_EQ(receipt->allowance, Allowance::SPELL);
	EXPECT_EQ(receipt->source, Source::PERK);
	EXPECT_NE(receipt->grantId, hero->grantId);
	EXPECT_NE(receipt->grantId, laterSpell);
}

TEST(HeroActionAllowanceState, EarliestExpiryThenStableIdBreaksSpecializedTies)
{
	Ledger ledger;
	ledger.resetForRound(4);
	const auto first = ledger.grantAllowance(Allowance::SPELL, Source::METAMAGIC, 6);
	const auto second = ledger.grantAllowance(Allowance::SPELL, Source::PERK, 6);
	const auto earlierExpiry = ledger.grantAllowance(Allowance::SPELL, Source::ARTIFACT, 5);

	auto selected = ledger.eligibleAllowance(Action::SPELL, 4);
	ASSERT_TRUE(selected);
	EXPECT_EQ(selected->grantId, earlierExpiry);

	ASSERT_TRUE(ledger.consumeAllowance(earlierExpiry, Action::SPELL, 4));
	selected = ledger.eligibleAllowance(Action::SPELL, 4);
	ASSERT_TRUE(selected);
	EXPECT_EQ(selected->grantId, first);
	EXPECT_NE(selected->grantId, second);
}

TEST(HeroActionAllowanceState, WrongKindAndRejectedConsumptionLeaveStateUnchanged)
{
	Ledger ledger;
	ledger.resetForRound(2);
	const auto spell = ledger.grantAllowance(Allowance::SPELL, Source::METAMAGIC, 2);
	const auto order = ledger.grantAllowance(Allowance::ORDER, Source::PERK, 2);
	const auto before = ledger;

	EXPECT_FALSE(ledger.consumeAllowance(order, Action::SPELL, 2));
	EXPECT_FALSE(ledger.consumeAllowance(spell, Action::ORDER, 2));
	EXPECT_FALSE(ledger.consumeAllowance(spell, Action::SPELL, 3));
	EXPECT_EQ(ledger, before);

	const auto selectedSpell = ledger.eligibleAllowance(Action::SPELL, 2);
	ASSERT_TRUE(selectedSpell);
	EXPECT_EQ(selectedSpell->grantId, spell);
	EXPECT_FALSE(ledger.consumeAllowance(1, Action::SPELL, 2)); // Hero is not selected while Spell is available.
	EXPECT_EQ(ledger, before);

	const auto selectedOrder = ledger.eligibleAllowance(Action::ORDER, 2);
	ASSERT_TRUE(selectedOrder);
	EXPECT_EQ(selectedOrder->grantId, order);
	const auto orderReceipt = ledger.consumeAllowance(selectedOrder->grantId, Action::ORDER, 2);
	ASSERT_TRUE(orderReceipt);
	EXPECT_EQ(orderReceipt->action, Action::ORDER);
	EXPECT_EQ(orderReceipt->allowance, Allowance::ORDER);
	EXPECT_EQ(orderReceipt->source, Source::PERK);
}

TEST(HeroActionAllowanceState, TypedGrantCannotPayOtherKindWithoutFlexibleAllowance)
{
	Ledger ledger;
	ledger.resetForRound(2);
	const auto base = ledger.eligibleAllowance(Action::SPELL, 2);
	ASSERT_TRUE(base);
	ASSERT_TRUE(ledger.consumeAllowance(base->grantId, Action::SPELL, 2));
	const auto order = ledger.grantAllowance(Allowance::ORDER, Source::PERK, 2);
	ASSERT_TRUE(ledger.eligibleAllowance(Action::ORDER, 2));
	EXPECT_FALSE(ledger.eligibleAllowance(Action::SPELL, 2));

	const auto before = ledger;
	EXPECT_FALSE(ledger.consumeAllowance(order, Action::SPELL, 2));
	EXPECT_EQ(ledger, before);
}

TEST(HeroActionAllowanceState, CountsAreSeparateAndIgnoreExpiredGrants)
{
	Ledger ledger;
	ledger.resetForRound(1);
	ledger.grantAllowance(Allowance::SPELL, Source::METAMAGIC, 1);
	ledger.grantAllowance(Allowance::ORDER, Source::PERK, 2);

	EXPECT_EQ(ledger.remainingCounts(1), (Ledger::Counts{1, 1, 1}));
	EXPECT_EQ(ledger.remainingCounts(2), (Ledger::Counts{0, 1, 0}));

	ledger.resetForRound(2);
	EXPECT_EQ(ledger.remainingCounts(2), (Ledger::Counts{1, 1, 0}));
	ledger.resetForRound(3);
	EXPECT_EQ(ledger.remainingCounts(3), (Ledger::Counts{1, 0, 0}));
}

TEST(HeroActionAllowanceState, RoundResetIsIdempotentAndRefreshesOneBaseAllowance)
{
	Ledger ledger;
	ledger.resetForRound(3);
	const auto initial = ledger.eligibleAllowance(Action::SPELL, 3);
	ASSERT_TRUE(initial);
	ASSERT_TRUE(ledger.consumeAllowance(initial->grantId, Action::SPELL, 3));

	ledger.resetForRound(3);
	EXPECT_FALSE(ledger.eligibleAllowance(Action::ORDER, 3));

	ledger.resetForRound(4);
	const auto nextRound = ledger.eligibleAllowance(Action::ORDER, 4);
	ASSERT_TRUE(nextRound);
	EXPECT_EQ(nextRound->allowance, Allowance::HERO);
	EXPECT_EQ(nextRound->source, Source::ROUND);
	ledger.resetForRound(4);
	EXPECT_EQ(ledger.remainingCounts(4), (Ledger::Counts{1, 0, 0}));
	EXPECT_THROW(ledger.resetForRound(3), std::invalid_argument);
}

TEST(HeroActionAllowanceState, ExpiresInclusivelyOnlyAfterExpiryRound)
{
	Ledger ledger;
	ledger.resetForRound(5);
	const auto spell = ledger.grantAllowance(Allowance::SPELL, Source::METAMAGIC, 6);

	ASSERT_TRUE(ledger.eligibleAllowance(Action::SPELL, 6));
	EXPECT_EQ(ledger.remainingCounts(6).spellActions, 1);
	EXPECT_FALSE(ledger.eligibleAllowance(Action::SPELL, 7));
	EXPECT_EQ(ledger.remainingCounts(7).spellActions, 0);

	ledger.resetForRound(7);
	EXPECT_EQ(ledger.remainingCounts(7), (Ledger::Counts{1, 0, 0}));
	EXPECT_FALSE(ledger.consumeAllowance(spell, Action::SPELL, 7));
}

TEST(HeroActionAllowanceState, RejectsInvalidGrantSourcesKindsRoundsAndOverflow)
{
	Ledger ledger;
	EXPECT_THROW(ledger.grantAllowance(Allowance::SPELL, Source::METAMAGIC, 0), std::invalid_argument);
	EXPECT_THROW(ledger.resetForRound(-1), std::invalid_argument);
	ledger.resetForRound(1);

	EXPECT_THROW(ledger.grantAllowance(Allowance::ORDER, Source::METAMAGIC, 1), std::invalid_argument);
	EXPECT_THROW(ledger.grantAllowance(Allowance::HERO, Source::ROUND, 1), std::invalid_argument);
	EXPECT_THROW(ledger.grantAllowance(Allowance::SPELL, Source::OTHER, 0), std::invalid_argument);
	EXPECT_THROW(ledger.eligibleAllowance(static_cast<Action>(99), 1), std::invalid_argument);
	EXPECT_THROW(ledger.eligibleAllowance(Action::ORDER, 0), std::invalid_argument);
	EXPECT_THROW(ledger.eligibleAllowance(Action::SPELL, 0), std::invalid_argument);

	const auto beforeOverflow = ledger;
	ledger.nextGrantId = std::numeric_limits<uint32_t>::max();
	const auto exhausted = ledger;
	EXPECT_THROW(ledger.grantAllowance(Allowance::ORDER, Source::PERK, 1), std::overflow_error);
	EXPECT_EQ(ledger, exhausted);
	EXPECT_NE(ledger, beforeOverflow);
}

TEST(HeroActionAllowanceState, ValidatesMalformedSerializedShape)
{
	Ledger ledger;
	ledger.resetForRound(1);
	ledger.grantAllowance(Allowance::SPELL, Source::METAMAGIC, 1);
	EXPECT_NO_THROW(ledger.validateShape());

	auto malformed = ledger;
	malformed.grants.back().allowance = static_cast<Allowance>(99);
	EXPECT_THROW(malformed.validateShape(), std::runtime_error);

	malformed = ledger;
	malformed.grants.back().source = Source::ROUND;
	EXPECT_THROW(malformed.validateShape(), std::runtime_error);

	malformed = ledger;
	malformed.grants.back().id = malformed.grants.front().id;
	EXPECT_THROW(malformed.validateShape(), std::runtime_error);

	malformed = ledger;
	malformed.nextGrantId = malformed.grants.back().id;
	EXPECT_THROW(malformed.validateShape(), std::runtime_error);

	malformed = ledger;
	malformed.grants.back().expiryRound = 0;
	EXPECT_THROW(malformed.validateShape(), std::runtime_error);
}

TEST(HeroActionAllowanceState, SerializationRoundTripsAndRejectsMalformedState)
{
	Ledger original;
	original.resetForRound(2);
	original.grantAllowance(Allowance::SPELL, Source::METAMAGIC, 2);
	original.grantAllowance(Allowance::ORDER, Source::PERK, 3);

	CMemorySerializer serializer;
	serializer.oser.version = ESerializationVersion::CURRENT;
	serializer.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(serializer.oser & original);

	Ledger restored;
	ASSERT_NO_THROW(serializer.iser & restored);
	EXPECT_EQ(restored, original);

	auto malformed = original;
	malformed.grants.back().source = static_cast<Source>(99);
	CMemorySerializer invalid;
	invalid.oser.version = ESerializationVersion::CURRENT;
	EXPECT_THROW(invalid.oser & malformed, std::runtime_error);
	EXPECT_TRUE(invalid.extractBuffer().empty());
}
