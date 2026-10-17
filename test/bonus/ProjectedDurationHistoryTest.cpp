/*
 * ProjectedDurationHistoryTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/bonuses/ProjectedBonusGraph.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include "../../lib/bonuses/Propagators.h"
#include <array>

namespace
{
TNodesVector nativeChildren(CBonusSystemNode & node)
{
	PreparedBonusGraphStorage captured;
	const auto index = captured.capture(node);
	return captured.projected(index).children; // Copy while the captured owner lives.
}

std::shared_ptr<Bonus> timed(int turns)
{
	auto record = std::make_shared<Bonus>(BonusDuration::N_TURNS,
		BonusType::STACKS_SPEED, BonusSource::OTHER, 2, BonusSourceID());
	record->turnsRemain = turns;
	return record;
}
}

TEST(ProjectedDurationHistoryTest, NativeSharedExportIdentityIsAgedAtEachOwnerAndRemovedInOrder)
{
	CBonusSystemNode root(BonusNodeType::ARMY), child(BonusNodeType::STACK_INSTANCE);
	child.attachTo(root);
	ASSERT_TRUE(vstd::contains(nativeChildren(root), &child));
	const auto record = timed(3);
	root.addNewBonus(record);
	child.addNewBonus(record); // Genuine one pointee exported by two owners.
	std::vector<ProjectedDurationChange> receipts;
	{
		ProjectedBonusGraph projected;
		projected.capture(root);
		receipts = projected.reduceDurationsRecording(root, Bonus::NTurns);
		ASSERT_EQ(receipts.size(), 2);
		EXPECT_EQ(receipts[0].source, &root);
		EXPECT_EQ(receipts[1].source, &child);
		EXPECT_EQ(receipts[0].after, receipts[1].before);
		EXPECT_EQ(projected.values(root).exported.front(), receipts[1].after);
		EXPECT_EQ(projected.values(child).exported.front(), receipts[1].after);
		EXPECT_EQ(receipts[1].after->turnsRemain, 1);
		EXPECT_EQ(record->turnsRemain, 3);
	}
	root.reduceBonusDurations(Bonus::NTurns);
	EXPECT_EQ(record->turnsRemain, receipts.back().after->turnsRemain);
	child.detachFrom(root);
	EXPECT_FALSE(vstd::contains(nativeChildren(root), &child));
}

TEST(ProjectedDurationHistoryTest, DiamondChildVisitsRemainOrderedAndReplayUsesExactOwnedValues)
{
	CBonusSystemNode root(BonusNodeType::ARMY), left(BonusNodeType::ARMY), right(BonusNodeType::ARMY), leaf(BonusNodeType::STACK_INSTANCE);
	left.attachTo(root);
	right.attachTo(root);
	leaf.attachTo(left);
	leaf.attachTo(right);
	ASSERT_TRUE(vstd::contains(nativeChildren(root), &left));
	ASSERT_TRUE(vstd::contains(nativeChildren(root), &right));
	ASSERT_TRUE(vstd::contains(nativeChildren(left), &leaf));
	ASSERT_TRUE(vstd::contains(nativeChildren(right), &leaf));
	const auto record = timed(2);
	leaf.addNewBonus(record);
	std::vector<ProjectedDurationChange> frozen;
	{
		ProjectedBonusGraph first, replay;
		first.capture(root);
		replay.capture(root);
		frozen = first.reduceDurationsRecording(root, Bonus::NTurns);
		ASSERT_EQ(frozen.size(), 2);
		EXPECT_FALSE(frozen[0].removed);
		EXPECT_TRUE(frozen[1].removed);
		replay.replayDurationChanges(frozen);
		EXPECT_TRUE(first.values(leaf).exported.empty());
		EXPECT_TRUE(replay.values(leaf).exported.empty());
		EXPECT_EQ(record->turnsRemain, 2);
	}
	root.reduceBonusDurations(Bonus::NTurns);
	EXPECT_TRUE(leaf.getExportedBonusList().empty());
	EXPECT_EQ(record->turnsRemain, 0);
	leaf.detachFrom(right);
	leaf.detachFrom(left);
	right.detachFrom(root);
	left.detachFrom(root);
	EXPECT_TRUE(nativeChildren(root).empty());
	EXPECT_TRUE(nativeChildren(left).empty());
	EXPECT_TRUE(nativeChildren(right).empty());
}

TEST(ProjectedDurationHistoryTest, SelectionIsWholeNodeSnapshotBeforeAnyRecordDecrement)
{
	CBonusSystemNode source(BonusNodeType::ARMY);
	const auto first = timed(2), second = timed(2);
	source.addNewBonus(first);
	source.addNewBonus(second);
	int selected = 0;
	const auto selector = CSelector([&selected](const Bonus * value)
	{
		++selected;
		EXPECT_EQ(value->turnsRemain, 2);
		return true;
	});
	{
		ProjectedBonusGraph projected;
		projected.capture(source);
		const auto changes = projected.reduceDurationsRecording(source, selector);
		ASSERT_EQ(changes.size(), 2);
		EXPECT_EQ(selected, 2);
		EXPECT_EQ(first->turnsRemain, 2);
		EXPECT_EQ(second->turnsRemain, 2);
	}
	selected = 0;
	source.reduceBonusDurations(selector);
	EXPECT_EQ(selected, 2);
	EXPECT_EQ(first->turnsRemain, 1);
	EXPECT_EQ(second->turnsRemain, 1);
}

TEST(ProjectedDurationHistoryTest, OldOpaqueIdentityRemovesOnlyTheLocalLatestCopyAfterClockAndReplay)
{
	CBonusSystemNode source(BonusNodeType::ARMY);
	const auto record = timed(3);
	source.addNewBonus(record);
	ProjectedBonusGraph parent, left, right;
	parent.capture(source);
	left.capture(source);
	right.capture(source);
	const auto changes = parent.reduceDurationsRecording(source, Bonus::NTurns);
	left.replayDurationChanges(changes);
	right.replayDurationChanges(changes);
	left.reduceDurationsRecording(source, Bonus::NTurns);
	left.removeExported(source, record);
	EXPECT_TRUE(left.values(source).exported.empty());
	EXPECT_EQ(right.values(source).exported.front()->turnsRemain, 2);
	EXPECT_EQ(parent.values(source).exported.front()->turnsRemain, 2);
	EXPECT_EQ(record->turnsRemain, 3);
}

TEST(ProjectedDurationHistoryTest, SameOwnershipControlBlockDoesNotMergeDistinctNativePointees)
{
	CBonusSystemNode source(BonusNodeType::ARMY);
	const auto owner = std::make_shared<std::array<Bonus, 2>>();
	(*owner)[0] = *timed(3);
	(*owner)[1] = *timed(5);
	const std::shared_ptr<Bonus> first(owner, &(*owner)[0]), second(owner, &(*owner)[1]);
	source.addNewBonus(first);
	source.addNewBonus(second);
	{
		ProjectedBonusGraph projected;
		projected.capture(source);
		const auto changes = projected.reduceDurationsRecording(source, Bonus::NTurns);
		ASSERT_EQ(changes.size(), 2);
		EXPECT_EQ(changes[0].after->turnsRemain, 2);
		EXPECT_EQ(changes[1].after->turnsRemain, 4);
		EXPECT_NE(changes[0].after, changes[1].after);
		EXPECT_EQ(first->turnsRemain, 3);
		EXPECT_EQ(second->turnsRemain, 5);
	}
	source.reduceBonusDurations(Bonus::NTurns);
	EXPECT_EQ(first->turnsRemain, 2);
	EXPECT_EQ(second->turnsRemain, 4);
}

TEST(ProjectedDurationHistoryTest, LaterAddWithOriginalIdentityUsesTheCurrentPrivatePointee)
{
	CBonusSystemNode first(BonusNodeType::ARMY), second(BonusNodeType::ARMY);
	const auto record = timed(3);
	first.addNewBonus(record);
	{
		ProjectedBonusGraph projected;
		projected.capture(first);
		projected.capture(second);
		projected.reduceDurationsRecording(first, Bonus::NTurns);
		projected.addExported(second, record);
		EXPECT_EQ(projected.values(second).exported.front()->turnsRemain, 2);
		EXPECT_EQ(projected.values(first).exported.front(), projected.values(second).exported.front());
		projected.reduceDurationsRecording(second, Bonus::NTurns);
		EXPECT_EQ(projected.values(first).exported.front()->turnsRemain, 1);
		EXPECT_EQ(record->turnsRemain, 3);
	}
	first.reduceBonusDurations(Bonus::NTurns);
	second.addNewBonus(record);
	second.reduceBonusDurations(Bonus::NTurns);
	EXPECT_EQ(first.getExportedBonusList().front()->turnsRemain, 1);
}
