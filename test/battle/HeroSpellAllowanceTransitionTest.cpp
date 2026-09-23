/*
 * HeroSpellAllowanceTransitionTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../../lib/battle/HeroActionAllowanceState.h"

namespace
{
using Ledger = HeroActionAllowanceState;
using Action = Ledger::ActionKind;
using Allowance = Ledger::AllowanceKind;
using Source = Ledger::GrantSource;
using Transition = HeroSpellAllowanceTransition;

struct TransitionFixture
{
	Ledger ledger;
	int32_t round = 0;
	uint32_t selectionGrantId = 0;
	uint8_t metamagicUsesConsumed = 0;
	uint8_t metamagicPendingCount = 0;
	bool metamagicGrandUsed = false;
};

TransitionFixture makeSpellGrantFixture(Source source, int32_t round = 0)
{
	TransitionFixture fixture;
	fixture.round = round;
	fixture.ledger.resetForRound(round);
	fixture.selectionGrantId = fixture.ledger.grantAllowance(Allowance::SPELL, source, round);
	if(source == Source::METAMAGIC || source == Source::METAMAGIC_GRAND)
		fixture.metamagicPendingCount = 1;
	return fixture;
}

template<typename Attempt>
void expectRejectedAtomically(TransitionFixture & fixture, Attempt attempt)
{
	const auto ledgerBefore = fixture.ledger;
	const auto usesBefore = fixture.metamagicUsesConsumed;
	const auto pendingBefore = fixture.metamagicPendingCount;
	const auto grandUsedBefore = fixture.metamagicGrandUsed;

	EXPECT_FALSE(attempt().has_value());
	EXPECT_EQ(fixture.ledger, ledgerBefore);
	EXPECT_EQ(fixture.metamagicUsesConsumed, usesBefore);
	EXPECT_EQ(fixture.metamagicPendingCount, pendingBefore);
	EXPECT_EQ(fixture.metamagicGrandUsed, grandUsedBefore);
}
}

TEST(HeroSpellAllowanceTransition, BaseHeroCastReservesOneMetamagicGrantAndFirstExtraChargesOnce)
{
	TransitionFixture fixture;
	fixture.round = 2;
	fixture.ledger.resetForRound(fixture.round);
	const auto orderGrant = fixture.ledger.grantAllowance(Allowance::ORDER, Source::PERK, fixture.round);
	auto selection = fixture.ledger.eligibleAllowance(Action::SPELL, fixture.round);
	ASSERT_TRUE(selection);
	EXPECT_EQ(selection->allowance, Allowance::HERO);

	size_t sequenceSpellCount = 0;
	const auto baseCast = Transition::commitAcceptedCast(fixture.ledger, selection->grantId, fixture.round,
		false, false, 1, false, fixture.metamagicUsesConsumed, fixture.metamagicPendingCount,
		fixture.metamagicGrandUsed, sequenceSpellCount);
	ASSERT_TRUE(baseCast);
	EXPECT_EQ(baseCast->receipt.allowance, Allowance::HERO);
	EXPECT_EQ(baseCast->receipt.source, Source::ROUND);
	EXPECT_FALSE(baseCast->chargedMetamagicUse);
	EXPECT_FALSE(baseCast->activatedGrand);
	ASSERT_EQ(baseCast->grantedGrantIds.size(), 1);
	EXPECT_EQ(baseCast->pendingMetamagicGrants, 1);
	EXPECT_EQ(fixture.metamagicUsesConsumed, 0);
	EXPECT_EQ(fixture.metamagicPendingCount, 1);
	EXPECT_EQ(sequenceSpellCount, 0);
	EXPECT_EQ(fixture.ledger.grants.back().source, Source::METAMAGIC);
	EXPECT_EQ(fixture.ledger.remainingCounts(fixture.round), (Ledger::Counts{0, 1, 1}));

	++sequenceSpellCount; // The caller records the accepted base spell in its sequence.
	selection = fixture.ledger.eligibleAllowance(Action::SPELL, fixture.round);
	ASSERT_TRUE(selection);
	EXPECT_EQ(selection->grantId, baseCast->grantedGrantIds.front());
	const auto extraCast = Transition::commitAcceptedCast(fixture.ledger, selection->grantId, fixture.round,
		true, false, 1, false, fixture.metamagicUsesConsumed, fixture.metamagicPendingCount,
		fixture.metamagicGrandUsed, sequenceSpellCount);
	ASSERT_TRUE(extraCast);
	EXPECT_EQ(extraCast->receipt.source, Source::METAMAGIC);
	EXPECT_TRUE(extraCast->chargedMetamagicUse);
	EXPECT_FALSE(extraCast->activatedGrand);
	EXPECT_TRUE(extraCast->grantedGrantIds.empty());
	EXPECT_EQ(extraCast->pendingMetamagicGrants, 0);
	EXPECT_EQ(fixture.metamagicUsesConsumed, 1);
	EXPECT_EQ(fixture.metamagicPendingCount, 0);
	EXPECT_FALSE(fixture.metamagicGrandUsed);
	EXPECT_EQ(sequenceSpellCount, 1); // Sequence metadata stays caller-owned.
	EXPECT_EQ(fixture.ledger.remainingCounts(fixture.round), (Ledger::Counts{0, 1, 0}));
	EXPECT_EQ(fixture.ledger.grants.front().id, orderGrant);
}

TEST(HeroSpellAllowanceTransition, GrandExtraGrantsOneFollowUpAndSecondExtraDoesNotCharge)
{
	TransitionFixture fixture;
	fixture.round = 3;
	fixture.ledger.resetForRound(fixture.round);
	auto selection = fixture.ledger.eligibleAllowance(Action::SPELL, fixture.round);
	ASSERT_TRUE(selection);

	size_t sequenceSpellCount = 0;
	const auto baseCast = Transition::commitAcceptedCast(fixture.ledger, selection->grantId, fixture.round,
		false, false, 3, true, fixture.metamagicUsesConsumed, fixture.metamagicPendingCount,
		fixture.metamagicGrandUsed, sequenceSpellCount);
	ASSERT_TRUE(baseCast);
	EXPECT_FALSE(baseCast->chargedMetamagicUse);
	ASSERT_EQ(baseCast->grantedGrantIds.size(), 1);
	++sequenceSpellCount;

	selection = fixture.ledger.eligibleAllowance(Action::SPELL, fixture.round);
	ASSERT_TRUE(selection);
	const auto firstExtra = Transition::commitAcceptedCast(fixture.ledger, selection->grantId, fixture.round,
		true, true, 3, true, fixture.metamagicUsesConsumed, fixture.metamagicPendingCount,
		fixture.metamagicGrandUsed, sequenceSpellCount);
	ASSERT_TRUE(firstExtra);
	EXPECT_TRUE(firstExtra->chargedMetamagicUse);
	EXPECT_TRUE(firstExtra->activatedGrand);
	ASSERT_EQ(firstExtra->grantedGrantIds.size(), 1);
	EXPECT_EQ(firstExtra->pendingMetamagicGrants, 1);
	EXPECT_EQ(fixture.metamagicUsesConsumed, 1);
	EXPECT_EQ(fixture.metamagicPendingCount, 1);
	EXPECT_TRUE(fixture.metamagicGrandUsed);
	EXPECT_EQ(fixture.ledger.grants.back().source, Source::METAMAGIC_GRAND);
	EXPECT_EQ(sequenceSpellCount, 1);

	++sequenceSpellCount; // The caller appends the accepted first extra cast.
	selection = fixture.ledger.eligibleAllowance(Action::SPELL, fixture.round);
	ASSERT_TRUE(selection);
	const auto secondExtra = Transition::commitAcceptedCast(fixture.ledger, selection->grantId, fixture.round,
		true, false, 3, true, fixture.metamagicUsesConsumed, fixture.metamagicPendingCount,
		fixture.metamagicGrandUsed, sequenceSpellCount);
	ASSERT_TRUE(secondExtra);
	EXPECT_EQ(secondExtra->receipt.source, Source::METAMAGIC_GRAND);
	EXPECT_FALSE(secondExtra->chargedMetamagicUse);
	EXPECT_FALSE(secondExtra->activatedGrand);
	EXPECT_TRUE(secondExtra->grantedGrantIds.empty());
	EXPECT_EQ(secondExtra->pendingMetamagicGrants, 0);
	EXPECT_EQ(fixture.metamagicUsesConsumed, 1);
	EXPECT_EQ(fixture.metamagicPendingCount, 0);
	EXPECT_TRUE(fixture.metamagicGrandUsed);
	EXPECT_EQ(sequenceSpellCount, 2);
}

TEST(HeroSpellAllowanceTransition, RejectsProtocolFollowUpFromWrongSourceAtomically)
{
	auto fixture = makeSpellGrantFixture(Source::PERK);
	expectRejectedAtomically(fixture, [&fixture]()
	{
		return Transition::commitAcceptedCast(fixture.ledger, fixture.selectionGrantId, fixture.round,
			true, false, 1, false, fixture.metamagicUsesConsumed, fixture.metamagicPendingCount,
			fixture.metamagicGrandUsed, 1);
	});

	fixture = makeSpellGrantFixture(Source::METAMAGIC);
	expectRejectedAtomically(fixture, [&fixture]()
	{
		return Transition::commitAcceptedCast(fixture.ledger, fixture.selectionGrantId, fixture.round,
			false, false, 1, false, fixture.metamagicUsesConsumed, fixture.metamagicPendingCount,
			fixture.metamagicGrandUsed, 1);
	});
}

TEST(HeroSpellAllowanceTransition, RejectsInvalidGrandActivationAtomically)
{
	auto fixture = makeSpellGrantFixture(Source::METAMAGIC);
	expectRejectedAtomically(fixture, [&fixture]()
	{
		return Transition::commitAcceptedCast(fixture.ledger, fixture.selectionGrantId, fixture.round,
			true, true, 2, true, fixture.metamagicUsesConsumed, fixture.metamagicPendingCount,
			fixture.metamagicGrandUsed, 1);
	});

	expectRejectedAtomically(fixture, [&fixture]()
	{
		return Transition::commitAcceptedCast(fixture.ledger, fixture.selectionGrantId, fixture.round,
			true, true, 3, false, fixture.metamagicUsesConsumed, fixture.metamagicPendingCount,
			fixture.metamagicGrandUsed, 1);
	});

	fixture.metamagicGrandUsed = true;
	expectRejectedAtomically(fixture, [&fixture]()
	{
		return Transition::commitAcceptedCast(fixture.ledger, fixture.selectionGrantId, fixture.round,
			true, true, 3, true, fixture.metamagicUsesConsumed, fixture.metamagicPendingCount,
			fixture.metamagicGrandUsed, 1);
	});
}

TEST(HeroSpellAllowanceTransition, RejectsInvalidSequenceShapeAtomically)
{
	auto metamagic = makeSpellGrantFixture(Source::METAMAGIC);
	expectRejectedAtomically(metamagic, [&metamagic]()
	{
		return Transition::commitAcceptedCast(metamagic.ledger, metamagic.selectionGrantId, metamagic.round,
			true, false, 1, false, metamagic.metamagicUsesConsumed, metamagic.metamagicPendingCount,
			metamagic.metamagicGrandUsed, 0);
	});

	auto grand = makeSpellGrantFixture(Source::METAMAGIC_GRAND, 4);
	grand.metamagicUsesConsumed = 1;
	grand.metamagicGrandUsed = true;
	expectRejectedAtomically(grand, [&grand]()
	{
		return Transition::commitAcceptedCast(grand.ledger, grand.selectionGrantId, grand.round,
			true, false, 3, true, grand.metamagicUsesConsumed, grand.metamagicPendingCount,
			grand.metamagicGrandUsed, 1);
	});
}

TEST(HeroSpellAllowanceTransition, CannotSpendAnOrderAllowanceAsASpell)
{
	TransitionFixture fixture;
	fixture.round = 5;
	fixture.ledger.resetForRound(fixture.round);
	const auto baseSpell = fixture.ledger.eligibleAllowance(Action::SPELL, fixture.round);
	ASSERT_TRUE(baseSpell);
	ASSERT_TRUE(fixture.ledger.consumeAllowance(baseSpell->grantId, Action::SPELL, fixture.round));
	fixture.selectionGrantId = fixture.ledger.grantAllowance(Allowance::ORDER, Source::PERK, fixture.round);

	expectRejectedAtomically(fixture, [&fixture]()
	{
		return Transition::commitAcceptedCast(fixture.ledger, fixture.selectionGrantId, fixture.round,
			false, false, 1, false, fixture.metamagicUsesConsumed, fixture.metamagicPendingCount,
			fixture.metamagicGrandUsed, 0);
	});
	EXPECT_EQ(fixture.ledger.remainingCounts(fixture.round), (Ledger::Counts{0, 1, 0}));
}

TEST(HeroSpellAllowanceTransition, ExhaustedMetamagicBudgetDoesNotGrantAnotherSpell)
{
	TransitionFixture fixture;
	fixture.round = 6;
	fixture.ledger.resetForRound(fixture.round);
	const auto orderGrant = fixture.ledger.grantAllowance(Allowance::ORDER, Source::PERK, fixture.round);
	fixture.metamagicUsesConsumed = 1;
	const auto selection = fixture.ledger.eligibleAllowance(Action::SPELL, fixture.round);
	ASSERT_TRUE(selection);

	const auto cast = Transition::commitAcceptedCast(fixture.ledger, selection->grantId, fixture.round,
		false, false, 1, false, fixture.metamagicUsesConsumed, fixture.metamagicPendingCount,
		fixture.metamagicGrandUsed, 0);
	ASSERT_TRUE(cast);
	EXPECT_EQ(cast->receipt.allowance, Allowance::HERO);
	EXPECT_FALSE(cast->chargedMetamagicUse);
	EXPECT_FALSE(cast->activatedGrand);
	EXPECT_TRUE(cast->grantedGrantIds.empty());
	EXPECT_EQ(cast->pendingMetamagicGrants, 0);
	EXPECT_EQ(fixture.metamagicUsesConsumed, 1);
	EXPECT_EQ(fixture.metamagicPendingCount, 0);
	EXPECT_FALSE(fixture.metamagicGrandUsed);
	EXPECT_EQ(fixture.ledger.remainingCounts(fixture.round), (Ledger::Counts{0, 1, 0}));
	EXPECT_EQ(fixture.ledger.grants.front().id, orderGrant);
}

TEST(HeroSpellAllowanceTransition, OtherSpellSourceDoesNotSpendHeroActionOrTriggerMetamagic)
{
	auto fixture = makeSpellGrantFixture(Source::ARTIFACT, 2);
	const auto spell = Transition::commitAcceptedCast(fixture.ledger, fixture.selectionGrantId, fixture.round,
		false, false, 1, false, fixture.metamagicUsesConsumed, fixture.metamagicPendingCount,
		fixture.metamagicGrandUsed, 0);
	ASSERT_TRUE(spell);
	EXPECT_EQ(spell->receipt.source, Source::ARTIFACT);
	EXPECT_TRUE(spell->grantedGrantIds.empty());
	EXPECT_EQ(fixture.metamagicUsesConsumed, 0);
	EXPECT_EQ(fixture.metamagicPendingCount, 0);
	EXPECT_EQ(fixture.ledger.remainingCounts(fixture.round), (Ledger::Counts{1, 0, 0}));

	const auto hero = fixture.ledger.eligibleAllowance(Action::SPELL, fixture.round);
	ASSERT_TRUE(hero);
	const auto baseSpell = Transition::commitAcceptedCast(fixture.ledger, hero->grantId, fixture.round,
		false, false, 1, false, fixture.metamagicUsesConsumed, fixture.metamagicPendingCount,
		fixture.metamagicGrandUsed, 0);
	ASSERT_TRUE(baseSpell);
	EXPECT_EQ(baseSpell->receipt.allowance, Allowance::HERO);
	EXPECT_EQ(fixture.ledger.remainingCounts(fixture.round), (Ledger::Counts{0, 0, 1}));
	EXPECT_EQ(fixture.metamagicPendingCount, 1);
}
