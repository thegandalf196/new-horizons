/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/bonuses/ProjectedBonusGraph.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"

TEST(ProjectedEffectRefreshTest, RefreshPreservesSharedRawIdentityWithoutChangingNativeFieldsOrGeneration)
{
	CBonusSystemNode root(BonusNodeType::STACK_INSTANCE), inherited(BonusNodeType::ARMY);
	root.attachTo(inherited);
	auto clock = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::TIME_STOP,
		BonusSource::OTHER, 1, BonusSourceID());
	clock->turnsRemain = 1; clock->statusIdentity = "refresh-clock";
	clock->statusTags = {BonusStatusTag::DEBUFF};
	root.addNewBonus(clock); inherited.addNewBonus(clock);
	const auto generation = PreparedBonusGraphStorage::currentGeneration(), version = root.getTreeVersion();
	ProjectedBonusGraph graph; graph.capture(root);
	EXPECT_EQ(graph.values(root).exported.front(), clock); // No private replacement before an actual edit.
	Bonus incoming(*clock); incoming.turnsRemain = 4;
	incoming.statusTags.push_back(BonusStatusTag::NON_TRANSFERABLE);
	graph.refreshExported(root, incoming);
	const auto current = graph.values(root).exported.front();
	EXPECT_NE(current, clock);
	EXPECT_EQ(current, graph.values(inherited).exported.front());
	EXPECT_EQ(current->turnsRemain, 4);
	EXPECT_EQ(current->val, 1);
	EXPECT_EQ(current->statusTags, incoming.statusTags);
	EXPECT_EQ(clock->turnsRemain, 1);
	EXPECT_EQ(clock->statusTags, std::vector<BonusStatusTag>{BonusStatusTag::DEBUFF});
	graph.removeExported(root, clock); // An original exact handle resolves its ordered replacement.
	EXPECT_TRUE(graph.values(root).exported.empty());
	EXPECT_EQ(graph.values(inherited).exported.front(), current);
	EXPECT_EQ(graph.query(root, Selector::type()(BonusType::TIME_STOP), false)->size(), 1u);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_EQ(root.getTreeVersion(), version);
}

TEST(ProjectedEffectRefreshTest, LaterCapturedAliasUsesCurrentPrivateValueAndEmptyRefreshStillRecordsOneEvent)
{
	CBonusSystemNode root(BonusNodeType::STACK_INSTANCE), later(BonusNodeType::STACK_INSTANCE);
	auto record = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::TIME_STOP,
		BonusSource::OTHER, 1, BonusSourceID());
	record->turnsRemain = 1; record->statusIdentity = "shared-disconnected-clock";
	root.addNewBonus(record); later.addNewBonus(record);
	ProjectedBonusGraph graph; graph.capture(root);
	Bonus incoming(*record); incoming.turnsRemain = 5;
	graph.refreshExported(root, incoming);
	graph.capture(later); // A later form/source capture must not resurrect a preceding raw alias.
	EXPECT_EQ(graph.values(root).exported.front(), graph.values(later).exported.front());
	EXPECT_EQ(graph.values(later).exported.front()->turnsRemain, 5);
	EXPECT_EQ(later.getExportedBonusList().front()->turnsRemain, 1);
	const auto count = graph.invalidationEvents().size();
	incoming.statusIdentity = "missing-local-status";
	graph.refreshExported(root, incoming);
	EXPECT_EQ(graph.invalidationEvents().size(), count + 1);
	EXPECT_EQ(graph.values(root).exported.front()->turnsRemain, 5);
}
