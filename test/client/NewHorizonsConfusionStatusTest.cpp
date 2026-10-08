/*
 * NewHorizonsConfusionStatusTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt file in main folder
 */
#include "../StdInc.h"
#include "../server/battles/HeroCommandFixture.h"

#include "../../client/battle/NewHorizonsBattleStatus.h"
#include "../../lib/battle/NewHorizonsConfusionControl.h"
#include "../../lib/networkPacks/SetStackEffect.h"

class NewHorizonsConfusionStatusTest : public HeroCommandFixture
{
protected:
	using Behavior = battle::ConfusionBehavior;
	using Status = newHorizonsBattleStatus::ConfusionStatus;
	CStack * target = nullptr;
	SpellID confusion = SpellID::NONE;

	void prepareStatus()
	{
		prepareCommands();
		confusion = SpellID(SpellID::decode("new-horizons:confusion"));
		ASSERT_TRUE(confusion.hasValue()) << "Confusion must be registered, even while its cast remains inactive";
		target = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(120), 10);
	}

	Bonus apply(bool confounder = false, PlayerColor caster = PlayerColor(0))
	{
		const Bonus marker = newHorizonsConfusionControl::pendingMarker(confusion, caster, confounder);
		SetStackEffect added;
		added.battleID = BattleID(0);
		added.toAdd.emplace_back(target->unitId(), std::vector<Bonus>{marker});
		gameHandler->sendAndApply(added);
		return marker;
	}

	Status status()
	{
		const auto markers = target->getBonuses(Selector::type()(BonusType::CONFUSION_PENDING));
		return newHorizonsBattleStatus::confusionStatus(*markers, target->acquireState()->confusionState);
	}

	std::string tooltip(const Status & value)
	{
		return newHorizonsBattleStatus::confusionTooltip("Authored Confusion description", value);
	}
};

TEST_F(NewHorizonsConfusionStatusTest, RealPendingMarkerMeansNextActivationNotZeroRoundsOrSelectedOutcome)
{
	ASSERT_NO_FATAL_FAILURE(prepareStatus());
	const auto marker = apply();
	ASSERT_EQ(marker.turnsRemain, 0);
	ASSERT_TRUE(newHorizonsConfusionControl::isPendingMarker(&marker));
	const auto current = status();
	ASSERT_TRUE(current.active());
	EXPECT_TRUE(current.pending);
	EXPECT_FALSE(current.confounder);
	EXPECT_EQ(current.caster, PlayerColor(0));
	EXPECT_EQ(current.previousResolved, Behavior::NONE);
	EXPECT_EQ(newHorizonsBattleStatus::CONFUSION_BADGE, "NEXT");
	const auto text = tooltip(current);
	EXPECT_THAT(text, ::testing::HasSubstr("pending next activation"));
	EXPECT_THAT(text, ::testing::HasSubstr("Attack, Defend, or Wander, with equal initial chances"));
	EXPECT_THAT(text, ::testing::HasSubstr("Dispel removes the pending effect"));
	EXPECT_THAT(text, ::testing::Not(::testing::HasSubstr("0 rounds")));
	EXPECT_THAT(text, ::testing::Not(::testing::HasSubstr("Previous resolved behavior")));
	EXPECT_THAT(text, ::testing::Not(::testing::HasSubstr("Captured Confounder")));
	EXPECT_TRUE(newHorizonsBattleStatus::isConfusion("new-horizons:confusion"));
	EXPECT_FALSE(newHorizonsBattleStatus::isConfusion("core:berserk"));
}

TEST_F(NewHorizonsConfusionStatusTest, CapturedConfounderAndRecastRetainActualResolvedHistory)
{
	ASSERT_NO_FATAL_FAILURE(prepareStatus());
	target->confusionState.recordResolved(Behavior::WANDER);
	apply(true);
	const auto captured = status();
	ASSERT_TRUE(captured.active());
	EXPECT_TRUE(captured.confounder);
	EXPECT_EQ(captured.previousResolved, Behavior::WANDER);
	EXPECT_THAT(tooltip(captured), ::testing::HasSubstr("Captured Confounder"));
	EXPECT_THAT(tooltip(captured), ::testing::HasSubstr("sole legal behavior may repeat"));
	EXPECT_THAT(tooltip(captured), ::testing::HasSubstr("Previous resolved behavior: Wander"));
	EXPECT_THAT(tooltip(captured), ::testing::HasSubstr("history, not an additional active effect"));
	apply(false, PlayerColor(1));
	const auto recast = status();
	ASSERT_TRUE(recast.active());
	EXPECT_FALSE(recast.confounder);
	EXPECT_EQ(recast.caster, PlayerColor(1));
	EXPECT_EQ(recast.previousResolved, Behavior::WANDER);
	EXPECT_EQ(target->getBonuses(Selector::type()(BonusType::CONFUSION_PENDING))->size(), 1);
	EXPECT_THAT(tooltip(recast), ::testing::Not(::testing::HasSubstr("Captured Confounder")));
}

TEST_F(NewHorizonsConfusionStatusTest, ActualMarkerRemovalClearsBadgeWithoutErasingHistory)
{
	ASSERT_NO_FATAL_FAILURE(prepareStatus());
	target->confusionState.recordResolved(Behavior::DEFEND);
	EXPECT_FALSE(status().active());
	EXPECT_TRUE(tooltip(status()).empty());
	const auto marker = apply(true);
	ASSERT_TRUE(status().active());
	SetStackEffect removed;
	removed.battleID = BattleID(0);
	removed.toRemove.emplace_back(target->unitId(), std::vector<Bonus>{marker});
	gameHandler->sendAndApply(removed);
	ASSERT_FALSE(target->hasBonusOfType(BonusType::CONFUSION_PENDING));
	ASSERT_FALSE(target->acquireState()->confusionState.pending);
	const auto history = status();
	EXPECT_FALSE(history.active());
	EXPECT_EQ(history.previousResolved, Behavior::DEFEND);
	EXPECT_TRUE(tooltip(history).empty());
	const auto empty = newHorizonsBattleStatus::confusionStatus(std::vector<std::shared_ptr<Bonus>>{}, battle::ConfusionState{});
	EXPECT_EQ(empty, Status{});
}

TEST_F(NewHorizonsConfusionStatusTest, MissingDuplicateMismatchedAndStaleMarkersStayInactive)
{
	ASSERT_NO_FATAL_FAILURE(prepareStatus());
	const auto marker = apply();
	const auto pending = target->acquireState()->confusionState;
	using Markers = std::vector<std::shared_ptr<Bonus>>;
	const auto valid = std::make_shared<Bonus>(marker);
	EXPECT_FALSE(newHorizonsBattleStatus::confusionStatus(Markers{}, pending).active());
	EXPECT_FALSE(newHorizonsBattleStatus::confusionStatus(Markers{nullptr}, pending).active());
	EXPECT_FALSE(newHorizonsBattleStatus::confusionStatus(Markers{valid, valid}, pending).active());
	auto wrongCaster = std::make_shared<Bonus>(marker);
	wrongCaster->spellCasterOwner = PlayerColor(1);
	EXPECT_FALSE(newHorizonsBattleStatus::confusionStatus(Markers{wrongCaster}, pending).active());
	auto wrongCapturedPerk = std::make_shared<Bonus>(marker);
	wrongCapturedPerk->val = 2;
	EXPECT_FALSE(newHorizonsBattleStatus::confusionStatus(Markers{wrongCapturedPerk}, pending).active());
	for(const auto spell : {SpellID::HYPNOTIZE, SpellID::MAGIC_ARROW})
	{
		const auto otherSpell = std::make_shared<Bonus>(newHorizonsConfusionControl::pendingMarker(
			spell, pending.pendingCaster, pending.pendingConfounder));
		ASSERT_TRUE(newHorizonsConfusionControl::isPendingMarker(otherSpell.get()));
		EXPECT_FALSE(newHorizonsBattleStatus::confusionStatus(Markers{otherSpell}, pending).active());
	}
	auto cleared = pending;
	cleared.clearPending();
	EXPECT_FALSE(newHorizonsBattleStatus::confusionStatus(Markers{valid}, cleared).active());
	auto invalidState = pending;
	invalidState.pendingCaster = PlayerColor::CANNOT_DETERMINE;
	EXPECT_FALSE(newHorizonsBattleStatus::confusionStatus(Markers{valid}, invalidState).active());
}

TEST_F(NewHorizonsConfusionStatusTest, MalformedPendingMetadataDoesNotBecomeAnActiveBadge)
{
	ASSERT_NO_FATAL_FAILURE(prepareStatus());
	const auto marker = apply();
	const auto pending = target->acquireState()->confusionState;
	std::vector<Bonus> malformed(5, marker);
	malformed[0].duration = BonusDuration::N_TURNS;
	malformed[1].turnsRemain = 1;
	malformed[2].source = BonusSource::ARTIFACT;
	malformed[3].val = 3;
	malformed[4].statusIdentity = "unrelated";
	for(const auto & bad : malformed)
	{
		ASSERT_FALSE(newHorizonsConfusionControl::isPendingMarker(&bad));
		const auto hidden = newHorizonsBattleStatus::confusionStatus(
			std::vector<std::shared_ptr<Bonus>>{std::make_shared<Bonus>(bad)}, pending);
		EXPECT_FALSE(hidden.active());
		EXPECT_TRUE(tooltip(hidden).empty());
	}
}

TEST_F(NewHorizonsConfusionStatusTest, CompactSnapshotRefreshesPendingHistoryAndPrioritizesFeedback)
{
	ASSERT_NO_FATAL_FAILURE(prepareStatus());
	using namespace newHorizonsBattleStatus;
	StackInfoStatusSnapshot empty;
	apply();
	StackInfoStatusSnapshot pending;
	pending.confusion = status();
	EXPECT_NE(pending, empty);
	target->confusionState.recordResolved(Behavior::DEFEND);
	apply(true);
	StackInfoStatusSnapshot recast;
	recast.confusion = status();
	EXPECT_NE(recast, pending);
	EXPECT_TRUE(recast.confusion.confounder);
	EXPECT_EQ(recast.confusion.previousResolved, Behavior::DEFEND);
	target->confusionState.clearPending();
	StackInfoStatusSnapshot history;
	history.confusion = status();
	EXPECT_NE(history, recast);
	EXPECT_FALSE(history.confusion.active());
	target->confusionState.recordResolved(Behavior::WANDER);
	StackInfoStatusSnapshot changedHistory;
	changedHistory.confusion = status();
	EXPECT_NE(changedHistory, history);
	EXPECT_LT(stackStatusPriority(StackStatusIconKind::CONFUSION), stackStatusPriority(StackStatusIconKind::ORDINARY));
	const auto plan = stackStatusDisplayPlan({StackStatusIconKind::ORDINARY, StackStatusIconKind::CONFUSION}, 2);
	ASSERT_FALSE(plan.visibleEntryIndices.empty());
	EXPECT_EQ(plan.visibleEntryIndices.front(), 1);
}

TEST_F(NewHorizonsConfusionStatusTest, RepeatedReadbackDoesNotChangeRealUnitStateOrSendActions)
{
	ASSERT_NO_FATAL_FAILURE(prepareStatus());
	target->confusionState.recordResolved(Behavior::ATTACK);
	apply(true);
	const auto before = target->acquireState()->save();
	const auto allowances = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	const auto actions = server.startedActions.size();
	const auto orders = server.orderStateUpdates.size();
	const auto expected = status();
	for(int i = 0; i < 5; ++i)
	{
		EXPECT_EQ(status(), expected);
		EXPECT_THAT(tooltip(status()), ::testing::HasSubstr("Previous resolved behavior: Attack"));
	}
	EXPECT_EQ(target->acquireState()->save(), before);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), allowances);
	EXPECT_EQ(server.startedActions.size(), actions);
	EXPECT_EQ(server.orderStateUpdates.size(), orders);
}
