/*
 * AlternatingHeroActionStateTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../../lib/battle/AlternatingHeroActionState.h"
#include "../../lib/battle/NewHorizonsWarcasting.h"
#include "../../lib/serializer/CMemorySerializer.h"

#include <type_traits>

namespace
{
using State = AlternatingHeroActionState;
using Action = State::Action;

class MalformedStateReader
{
	std::vector<int32_t> values;
	size_t position = 0;
	bool includesActionSequence;

public:
	static constexpr bool saving = false;
	using Version = ESerializationVersion;

	explicit MalformedStateReader(std::vector<int32_t> values, bool includesActionSequence = false)
		: values(std::move(values)), includesActionSequence(includesActionSequence)
	{
	}

	bool hasFeature(Version feature) const
	{
		return includesActionSequence && feature == Version::NEW_HORIZONS_HERO_ACTION_SEQUENCE;
	}

	template <typename T, size_t N> MalformedStateReader & operator&(std::array<T, N> & array)
	{
		for(auto & value : array)
			*this & value;
		return *this;
	}

	template <typename T> MalformedStateReader & operator&(T & value)
	{
		const auto serializedValue = values.at(position++);
		if constexpr(std::is_enum_v<T>)
			value = static_cast<T>(serializedValue);
		else
			value = serializedValue;
		return *this;
	}
};

class AlternatingHeroActionRankTest : public testing::TestWithParam<int32_t>
{
};
}

TEST(AlternatingHeroActionState, DefaultsToNoReadiness)
{
	const State state;
	EXPECT_EQ(state.nextEligibleAction, Action::NONE);
	EXPECT_EQ(state.empowermentPercent, 0);
	EXPECT_EQ(state.expiryRound, 0);
	EXPECT_EQ(state.lastManaRecoveryRound, -1);
	EXPECT_EQ(state.recentActions, (std::array<Action, 3>{Action::NONE, Action::NONE, Action::NONE}));
	EXPECT_FALSE(state.hasRecentActionHistory());
	EXPECT_FALSE(state.hasAlternatingSpellOrderSequence());
	EXPECT_EQ(state.bonusFor(Action::SPELL, 1), 0);
	EXPECT_EQ(state.bonusFor(Action::ORDER, 1), 0);
}

TEST(AlternatingHeroActionState, RecognizesBothRightAlignedAlternatingSequencesAtZeroEmpowerment)
{
	State spellOrderSpell;
	EXPECT_EQ(spellOrderSpell.recordAcceptedAction(Action::SPELL, 1, 0), 0);
	EXPECT_EQ(spellOrderSpell.recentActions,
		(std::array<Action, 3>{Action::NONE, Action::NONE, Action::SPELL}));
	EXPECT_EQ(spellOrderSpell.recordAcceptedAction(Action::ORDER, 2, 0), 0);
	EXPECT_EQ(spellOrderSpell.recentActions,
		(std::array<Action, 3>{Action::NONE, Action::SPELL, Action::ORDER}));
	EXPECT_EQ(spellOrderSpell.recordAcceptedAction(Action::SPELL, 3, 0), 0);
	EXPECT_EQ(spellOrderSpell.recentActions,
		(std::array<Action, 3>{Action::SPELL, Action::ORDER, Action::SPELL}));
	EXPECT_TRUE(spellOrderSpell.hasAlternatingSpellOrderSequence());

	State orderSpellOrder;
	orderSpellOrder.recordAcceptedAction(Action::ORDER, 1, 0);
	orderSpellOrder.recordAcceptedAction(Action::SPELL, 2, 0);
	orderSpellOrder.recordAcceptedAction(Action::ORDER, 3, 0);
	EXPECT_EQ(orderSpellOrder.recentActions,
		(std::array<Action, 3>{Action::ORDER, Action::SPELL, Action::ORDER}));
	EXPECT_TRUE(orderSpellOrder.hasAlternatingSpellOrderSequence());
}

TEST(AlternatingHeroActionState, RepeatedActionsRollThroughTheThreeActionWindow)
{
	State state;
	state.recordAcceptedAction(Action::SPELL, 1, 0);
	state.recordAcceptedAction(Action::ORDER, 2, 0);
	state.recordAcceptedAction(Action::SPELL, 3, 0);
	ASSERT_TRUE(state.hasAlternatingSpellOrderSequence());

	state.recordAcceptedAction(Action::SPELL, 4, 0);
	EXPECT_EQ(state.recentActions,
		(std::array<Action, 3>{Action::ORDER, Action::SPELL, Action::SPELL}));
	EXPECT_FALSE(state.hasAlternatingSpellOrderSequence());
	state.recordAcceptedAction(Action::ORDER, 5, 0);
	EXPECT_EQ(state.recentActions,
		(std::array<Action, 3>{Action::SPELL, Action::SPELL, Action::ORDER}));
	EXPECT_FALSE(state.hasAlternatingSpellOrderSequence());
	state.recordAcceptedAction(Action::SPELL, 6, 0);
	EXPECT_EQ(state.recentActions,
		(std::array<Action, 3>{Action::SPELL, Action::ORDER, Action::SPELL}));
	EXPECT_TRUE(state.hasAlternatingSpellOrderSequence());
}

TEST(BattleMeditation, RefundCapacityDoesNotOverflowInt32Mana)
{
	const auto maxMana = std::numeric_limits<int32_t>::max();
	EXPECT_EQ(newHorizonsWarcasting::battleMeditationRecoveryAmount(maxMana), 0);
	EXPECT_EQ(newHorizonsWarcasting::battleMeditationRecoveryAmount(maxMana - 1), 1);
	EXPECT_EQ(newHorizonsWarcasting::battleMeditationRecoveryAmount(maxMana - 2), 2);
	EXPECT_EQ(newHorizonsWarcasting::battleMeditationRecoveryAmount(maxMana - 3), 3);
	EXPECT_EQ(newHorizonsWarcasting::battleMeditationRecoveryAmount(0), 3);
}

TEST_P(AlternatingHeroActionRankTest, StoresRankForTheOppositeAction)
{
	State state;
	const auto empowerment = GetParam();
	EXPECT_EQ(state.recordAcceptedAction(Action::SPELL, 3, empowerment), 0);

	if(empowerment == 0)
	{
		EXPECT_EQ(state.nextEligibleAction, Action::NONE);
		EXPECT_EQ(state.bonusFor(Action::ORDER, 3), 0);
	}
	else
	{
		EXPECT_EQ(state.nextEligibleAction, Action::ORDER);
		EXPECT_EQ(state.empowermentPercent, empowerment);
		EXPECT_EQ(state.bonusFor(Action::ORDER, 3), empowerment);
	}
}

INSTANTIATE_TEST_SUITE_P(DefaultAndEmpowermentRanks, AlternatingHeroActionRankTest,
	::testing::Values(0, 10, 20, 30));

TEST(AlternatingHeroActionState, DefaultLifetimeIncludesTheNextRound)
{
	State state;
	state.recordAcceptedAction(Action::SPELL, 3, 20);

	EXPECT_EQ(state.bonusFor(Action::ORDER, 4), 20);
	EXPECT_EQ(state.bonusFor(Action::ORDER, 5), 0);
}

TEST(AlternatingHeroActionState, ExtendedLifetimeIncludesTwoFollowingRounds)
{
	State state;
	state.recordAcceptedAction(Action::SPELL, 3, 30, 2);

	EXPECT_EQ(state.bonusFor(Action::ORDER, 5), 30);
	EXPECT_EQ(state.bonusFor(Action::ORDER, 6), 0);
}

TEST(AlternatingHeroActionState, RepeatingTheSameActionDoesNotConsumeAndRefreshesReadiness)
{
	State state;
	state.recordAcceptedAction(Action::SPELL, 2, 10);

	EXPECT_EQ(state.recordAcceptedAction(Action::SPELL, 3, 20), 0);
	EXPECT_EQ(state.bonusFor(Action::ORDER, 3), 20);
	EXPECT_EQ(state.bonusFor(Action::ORDER, 4), 20);
	EXPECT_EQ(state.bonusFor(Action::ORDER, 5), 0);
}

TEST(AlternatingHeroActionState, AlternatingActionsConsumeAndRearm)
{
	State state;
	EXPECT_EQ(state.recordAcceptedAction(Action::SPELL, 1, 10), 0);
	EXPECT_EQ(state.recordAcceptedAction(Action::ORDER, 2, 20), 10);
	EXPECT_EQ(state.nextEligibleAction, Action::SPELL);
	EXPECT_EQ(state.bonusFor(Action::SPELL, 3), 20);
	EXPECT_EQ(state.recordAcceptedAction(Action::SPELL, 3, 30), 20);
	EXPECT_EQ(state.nextEligibleAction, Action::ORDER);
	EXPECT_EQ(state.bonusFor(Action::ORDER, 4), 30);
}

TEST(AlternatingHeroActionState, QueriesDoNotMutateState)
{
	State state;
	state.recordAcceptedAction(Action::SPELL, 5, 20);
	const auto original = state;

	EXPECT_EQ(state.bonusFor(Action::ORDER, 5), 20);
	EXPECT_EQ(state.bonusFor(Action::SPELL, 5), 0);
	EXPECT_EQ(state.bonusFor(Action::ORDER, 7), 0);
	EXPECT_EQ(state.bonusFor(Action::ORDER, -1), 0);
	EXPECT_EQ(state, original);
}

TEST(AlternatingHeroActionState, ExpiredMatchingActionRearmsWithoutConsumingABonus)
{
	State state;
	state.recordAcceptedAction(Action::SPELL, 1, 20);
	EXPECT_EQ(state.recordAcceptedAction(Action::ORDER, 3, 30), 0);
	EXPECT_FALSE(state.hasConsumedBonus);
	EXPECT_EQ(state.bonusFor(Action::SPELL, 4), 30);
}

TEST(AlternatingHeroActionState, MasterSynthesisTracksOnlyPositiveMatchedConsumption)
{
	State unmatched;
	unmatched.recordAcceptedAction(Action::SPELL, 1, 20);
	EXPECT_EQ(unmatched.recordAcceptedAction(Action::SPELL, 2, 30), 0);
	EXPECT_FALSE(unmatched.hasConsumedBonus);

	State expired;
	expired.recordAcceptedAction(Action::SPELL, 1, 20);
	EXPECT_EQ(expired.recordAcceptedAction(Action::ORDER, 3, 30), 0);
	EXPECT_FALSE(expired.hasConsumedBonus);

	State consumed;
	consumed.recordAcceptedAction(Action::SPELL, 1, 20);
	EXPECT_EQ(consumed.recordAcceptedAction(Action::ORDER, 2, 0), 20);
	EXPECT_TRUE(consumed.hasConsumedBonus);
	EXPECT_EQ(consumed.nextEligibleAction, Action::NONE);
}

TEST(AlternatingHeroActionState, ExpiryClearingReturnsAChangedCopy)
{
	State state;
	state.recordAcceptedAction(Action::SPELL, 5, 20);
	const auto original = state;

	EXPECT_EQ(state.clearedIfExpired(6), original);
	const auto expired = state.clearedIfExpired(7);
	EXPECT_EQ(expired.nextEligibleAction, Action::NONE);
	EXPECT_EQ(expired.empowermentPercent, 0);
	EXPECT_EQ(expired.expiryRound, 0);
	EXPECT_EQ(expired.recentActions, original.recentActions);
	EXPECT_TRUE(expired.hasRecentActionHistory());
	EXPECT_EQ(state, original);
}

TEST(AlternatingHeroActionState, ReadinessExpiryPreservesHistoryForLaterActions)
{
	State state;
	state.recordAcceptedAction(Action::SPELL, 1, 20);
	const auto expired = state.clearedIfExpired(3);
	ASSERT_EQ(expired.nextEligibleAction, Action::NONE);
	EXPECT_EQ(expired.recentActions,
		(std::array<Action, 3>{Action::NONE, Action::NONE, Action::SPELL}));

	auto laterAction = expired;
	laterAction.recordAcceptedAction(Action::ORDER, 3, 0);
	EXPECT_EQ(laterAction.recentActions,
		(std::array<Action, 3>{Action::NONE, Action::SPELL, Action::ORDER}));
	EXPECT_FALSE(laterAction.hasAlternatingSpellOrderSequence());
	EXPECT_EQ(state.recentActions,
		(std::array<Action, 3>{Action::NONE, Action::NONE, Action::SPELL}));
}

TEST(AlternatingHeroActionState, ExpiryClearingPreservesBattleMeditationRound)
{
	State state;
	state.recordAcceptedAction(Action::SPELL, 5, 20);
	state.lastManaRecoveryRound = 6;

	const auto expired = state.clearedIfExpired(7);
	EXPECT_EQ(expired.nextEligibleAction, Action::NONE);
	EXPECT_EQ(expired.lastManaRecoveryRound, 6);
	EXPECT_EQ(state.lastManaRecoveryRound, 6);
}

TEST(AlternatingHeroActionState, MasterSynthesisConsumptionSurvivesReadinessExpiry)
{
	State state;
	state.recordAcceptedAction(Action::SPELL, 1, 10);
	EXPECT_EQ(state.recordAcceptedAction(Action::ORDER, 2, 20), 10);
	ASSERT_TRUE(state.hasConsumedBonus);

	const auto expired = state.clearedIfExpired(4);
	EXPECT_EQ(expired.nextEligibleAction, Action::NONE);
	EXPECT_TRUE(expired.hasConsumedBonus);
	EXPECT_TRUE(state.clearedIfExpired(3).hasConsumedBonus);
}

TEST(AlternatingHeroActionState, ZeroLifetimeExpiresInclusivelyAtTheCurrentRound)
{
	State state;
	const auto maxRound = std::numeric_limits<int32_t>::max();

	EXPECT_EQ(state.recordAcceptedAction(Action::SPELL, maxRound, 10, 0), 0);
	EXPECT_EQ(state.expiryRound, maxRound);
	EXPECT_EQ(state.bonusFor(Action::ORDER, maxRound), 10);
}

TEST(AlternatingHeroActionState, ZeroEmpowermentConsumesThenClearsReadiness)
{
	State state;
	state.recordAcceptedAction(Action::SPELL, 2, 20);

	EXPECT_EQ(state.recordAcceptedAction(Action::ORDER, 3, 0), 20);
	EXPECT_EQ(state.nextEligibleAction, Action::NONE);
	EXPECT_EQ(state.empowermentPercent, 0);
	EXPECT_EQ(state.expiryRound, 0);
	EXPECT_TRUE(state.hasConsumedBonus);
}

TEST(AlternatingHeroActionState, MasterSynthesisConsumptionIsBranchLocalAndRoundTrips)
{
	State original;
	original.recordAcceptedAction(Action::SPELL, 1, 20);
	State consumedBranch = original;
	State untouchedBranch = original;

	EXPECT_EQ(consumedBranch.recordAcceptedAction(Action::ORDER, 2, 30), 20);
	EXPECT_TRUE(consumedBranch.hasConsumedBonus);
	EXPECT_FALSE(original.hasConsumedBonus);
	EXPECT_FALSE(untouchedBranch.hasConsumedBonus);
	EXPECT_EQ(untouchedBranch.bonusFor(Action::ORDER, 2), 20);

	CMemorySerializer serializer;
	serializer.oser.version = ESerializationVersion::CURRENT;
	serializer.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(serializer.oser & consumedBranch);

	State restored;
	ASSERT_NO_THROW(serializer.iser & restored);
	EXPECT_EQ(restored, consumedBranch);
	EXPECT_TRUE(restored.hasConsumedBonus);
}

TEST(AlternatingHeroActionState, RejectsInvalidInputAndOverflowAtomically)
{
	State state;
	state.recordAcceptedAction(Action::SPELL, 3, 20);
	state.recordAcceptedAction(Action::ORDER, 4, 30);
	const auto original = state;

	EXPECT_THROW(state.recordAcceptedAction(Action::NONE, 4, 30), std::invalid_argument);
	EXPECT_EQ(state, original);
	EXPECT_THROW(state.recordAcceptedAction(static_cast<Action>(255), 4, 30), std::invalid_argument);
	EXPECT_EQ(state, original);
	EXPECT_THROW(state.recordAcceptedAction(Action::ORDER, -1, 30), std::invalid_argument);
	EXPECT_EQ(state, original);
	EXPECT_THROW(state.recordAcceptedAction(Action::ORDER, 4, -1), std::invalid_argument);
	EXPECT_EQ(state, original);
	EXPECT_THROW(state.recordAcceptedAction(Action::ORDER, 4, 30, -1), std::invalid_argument);
	EXPECT_EQ(state, original);
	EXPECT_THROW(state.recordAcceptedAction(Action::ORDER, std::numeric_limits<int32_t>::max(), 30), std::invalid_argument);
	EXPECT_EQ(state, original);
	EXPECT_THROW(state.clearedIfExpired(-1), std::invalid_argument);
	EXPECT_EQ(state, original);
}

TEST(AlternatingHeroActionState, SerializationRoundTripsAndValidatesShape)
{
	State original;
	original.recordAcceptedAction(Action::SPELL, 4, 30, 2);
	original.recordAcceptedAction(Action::ORDER, 5, 20, 2);
	original.recordAcceptedAction(Action::SPELL, 6, 10, 2);
	original.lastManaRecoveryRound = 5;

	CMemorySerializer serializer;
	serializer.oser.version = ESerializationVersion::CURRENT;
	serializer.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(serializer.oser & original);

	State restored;
	ASSERT_NO_THROW(serializer.iser & restored);
	EXPECT_EQ(restored, original);

	auto invalid = original;
	invalid.empowermentPercent = 0;
	EXPECT_THROW(invalid.validateShape(), std::runtime_error);
	invalid = original;
	invalid.recentActions = {Action::NONE, Action::SPELL, Action::NONE};
	EXPECT_THROW(invalid.validateShape(), std::runtime_error);
	invalid = original;
	invalid.recentActions = {Action::NONE, static_cast<Action>(255), Action::SPELL};
	EXPECT_THROW(invalid.validateShape(), std::runtime_error);

	State malformed;
	MalformedStateReader invalidInactiveState({static_cast<int32_t>(Action::NONE), 20, 4});
	EXPECT_THROW(malformed.serialize(invalidInactiveState), std::runtime_error);
	MalformedStateReader invalidAction({255, 20, 4});
	EXPECT_THROW(malformed.serialize(invalidAction), std::runtime_error);
	MalformedStateReader wrappedPositiveAction({257, 20, 4});
	EXPECT_THROW(malformed.serialize(wrappedPositiveAction), std::runtime_error);
	MalformedStateReader wrappedNegativeAction({-255, 20, 4});
	EXPECT_THROW(malformed.serialize(wrappedNegativeAction), std::runtime_error);
	MalformedStateReader negativeExpiry({static_cast<int32_t>(Action::ORDER), 20, -1});
	EXPECT_THROW(malformed.serialize(negativeExpiry), std::runtime_error);
	MalformedStateReader invalidRecentHistory({
		static_cast<int32_t>(Action::NONE), 0, 0,
		static_cast<int32_t>(Action::NONE), static_cast<int32_t>(Action::SPELL),
		static_cast<int32_t>(Action::NONE)}, true);
	EXPECT_THROW(malformed.serialize(invalidRecentHistory), std::runtime_error);
}

TEST(AlternatingHeroActionState, OlderWarcastingSaveDefaultsRecoveryRoundAndRejectsLossyWrite)
{
	State original;
	original.nextEligibleAction = Action::ORDER;
	original.empowermentPercent = 20;
	original.expiryRound = 5;
	CMemorySerializer oldVersion;
	oldVersion.oser.version = ESerializationVersion::NEW_HORIZONS_WARCASTING;
	oldVersion.iser.version = ESerializationVersion::NEW_HORIZONS_WARCASTING;
	oldVersion.oser & original;

	State restored;
	oldVersion.iser & restored;
	EXPECT_EQ(restored.nextEligibleAction, original.nextEligibleAction);
	EXPECT_EQ(restored.empowermentPercent, original.empowermentPercent);
	EXPECT_EQ(restored.expiryRound, original.expiryRound);
	EXPECT_EQ(restored.lastManaRecoveryRound, -1);
	EXPECT_FALSE(restored.hasRecentActionHistory());

	State recoveryOnly;
	recoveryOnly.lastManaRecoveryRound = 9;
	CMemorySerializer lossySave;
	lossySave.oser.version = ESerializationVersion::NEW_HORIZONS_WARCASTING;
	EXPECT_THROW(lossySave.oser & recoveryOnly, std::runtime_error);
	EXPECT_TRUE(lossySave.extractBuffer().empty());

	recoveryOnly.lastManaRecoveryRound = -2;
	EXPECT_THROW(recoveryOnly.validateShape(), std::runtime_error);
	CMemorySerializer malformed;
	malformed.oser.version = ESerializationVersion::CURRENT;
	EXPECT_THROW(malformed.oser & recoveryOnly, std::runtime_error);
	EXPECT_TRUE(malformed.extractBuffer().empty());
}

TEST(AlternatingHeroActionState, MasterSynthesisHistoryDefaultsForOldReadersAndRejectsOldWriters)
{
	State compatible;
	compatible.nextEligibleAction = Action::SPELL;
	compatible.empowermentPercent = 20;
	compatible.expiryRound = 5;
	CMemorySerializer oldVersion;
	oldVersion.oser.version = ESerializationVersion::NEW_HORIZONS_RAGE_THROUGH_PAIN;
	oldVersion.iser.version = ESerializationVersion::NEW_HORIZONS_RAGE_THROUGH_PAIN;
	ASSERT_NO_THROW(oldVersion.oser & compatible);

	State restored;
	ASSERT_NO_THROW(oldVersion.iser & restored);
	EXPECT_EQ(restored.nextEligibleAction, compatible.nextEligibleAction);
	EXPECT_EQ(restored.empowermentPercent, compatible.empowermentPercent);
	EXPECT_FALSE(restored.hasConsumedBonus);
	EXPECT_FALSE(restored.hasRecentActionHistory());

	State consumed;
	consumed.nextEligibleAction = Action::SPELL;
	consumed.empowermentPercent = 20;
	consumed.expiryRound = 2;
	consumed.hasConsumedBonus = true;
	ASSERT_TRUE(consumed.hasConsumedBonus);

	CMemorySerializer lossyWriter;
	lossyWriter.oser.version = ESerializationVersion::NEW_HORIZONS_RAGE_THROUGH_PAIN;
	EXPECT_THROW(lossyWriter.oser & consumed, std::runtime_error);
	EXPECT_TRUE(lossyWriter.extractBuffer().empty());
}

TEST(AlternatingHeroActionState, SequenceHistoryRoundTripsAndOldFormatReadsEmptyOrRejectsLoss)
{
	State original;
	original.recordAcceptedAction(Action::SPELL, 1, 0);
	original.recordAcceptedAction(Action::ORDER, 2, 0);
	original.recordAcceptedAction(Action::SPELL, 3, 0);
	ASSERT_TRUE(original.hasAlternatingSpellOrderSequence());

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & original);
	State restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored, original);
	EXPECT_TRUE(restored.hasAlternatingSpellOrderSequence());

	State oldCompatible;
	oldCompatible.nextEligibleAction = Action::ORDER;
	oldCompatible.empowermentPercent = 20;
	oldCompatible.expiryRound = 5;
	CMemorySerializer oldWire;
	oldWire.oser.version = ESerializationVersion::NEW_HORIZONS_ASTROLOGY_CONSTRUCTION_PREVIEW;
	oldWire.iser.version = ESerializationVersion::NEW_HORIZONS_ASTROLOGY_CONSTRUCTION_PREVIEW;
	ASSERT_NO_THROW(oldWire.oser & oldCompatible);
	State oldRead;
	ASSERT_NO_THROW(oldWire.iser & oldRead);
	EXPECT_EQ(oldRead.nextEligibleAction, Action::ORDER);
	EXPECT_EQ(oldRead.empowermentPercent, 20);
	EXPECT_EQ(oldRead.expiryRound, 5);
	EXPECT_EQ(oldRead.recentActions,
		(std::array<Action, 3>{Action::NONE, Action::NONE, Action::NONE}));
	EXPECT_FALSE(oldRead.hasRecentActionHistory());

	CMemorySerializer lossyOldWriter;
	lossyOldWriter.oser.version = ESerializationVersion::NEW_HORIZONS_ASTROLOGY_CONSTRUCTION_PREVIEW;
	EXPECT_THROW(lossyOldWriter.oser & original, std::runtime_error);
	EXPECT_TRUE(lossyOldWriter.extractBuffer().empty());
}

TEST(AlternatingHeroActionState, InactiveStateRoundTrips)
{
	State original;
	CMemorySerializer serializer;
	serializer.oser.version = ESerializationVersion::CURRENT;
	serializer.iser.version = ESerializationVersion::CURRENT;
	serializer.oser & original;
	State restored;
	restored.recordAcceptedAction(Action::ORDER, 3, 20);
	serializer.iser & restored;
	EXPECT_EQ(restored, original);
}
