/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/bonuses/OwnedRecipientBonusGraph.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/bonuses/Propagators.h"
#include <stdexcept>
TEST(OwnedSourceExportRoleTest, InheritedAliasKeepsExplicitSourceRoleWithoutRawQueryPromotion)
{
	OwnedRecipientBonusGraph::CaptureInput input; input.nodes.resize(2); input.nodes[0].parents = {1};
	input.nodes[1].facts.nodeType = BonusNodeType::HERO; input.nodes[1].facts.heroLevel = 3;
	input.nodes[1].facts.armySlots.emplace();
	auto raw = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 2, BonusSourceID());
	raw->updater = std::make_shared<TimesHeroLevelUpdater>(); input.nodes[1].local = {raw}; input.sourceExports = {raw};
	auto graphs = OwnedRecipientBonusGraph::fromOwnedBatchInputs({input}); const auto & graph = *graphs.front();
	EXPECT_EQ(graph.payload(0), graph.payload(graph.sourceExportRecord(0)));
	EXPECT_NO_THROW(graph.validateSourceExport(graph.sourceExportRecord(0)));
	OwnedRecipientBonusGraph::RootQuery query{input.nodes[0].facts, {}, {graph.sourceExportRecord(0)}};
	EXPECT_THROW(graph.validateRootQuery(query), std::invalid_argument);
	EXPECT_THROW(graph.extraRecord(0), std::out_of_range);
	EXPECT_EQ(graph.query(Selector::all).totalValue(), 6);
	raw->val = 90; EXPECT_EQ(graph.query(Selector::all).totalValue(), 6);
}
TEST(OwnedSourceExportRoleTest, PeerDependencyClosureRunsAfterOwnedRelationArrival)
{
	OwnedRecipientBonusGraph::CaptureInput input; input.nodes.resize(1);
	input.nodes[0].facts.nodeType = BonusNodeType::STACK_BATTLE;
	input.nodes[0].facts.stackCount = 2; input.nodes[0].facts.stableStackLevel = 7;
	input.nodes[0].facts.acceptance.emplace();
	auto unrelated = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 999, BonusSourceID());
	input.nodes[0].local = {unrelated}; input.peerDependenciesOnly = true;
	auto aura = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELL_RESISTANCE_AURA, BonusSource::OTHER, 25, BonusSourceID());
	aura->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::STACK_BATTLE); input.retainedRecords = {aura};
	OwnedBonusEvaluationFacts source; source.nodeType = BonusNodeType::CREATURE;
	const OwnedRecipientBonusGraph::SourceRelation relation{true, source, {1}, {0}};
	auto graphs = OwnedRecipientBonusGraph::fromOwnedBatchInputs({input}, {{relation}}, OwnedBonusCaptureCodec::current());
	EXPECT_EQ(graphs.front()->rootLocalRecords().size(), 1u);
	EXPECT_EQ(graphs.front()->query(Selector::all).totalValue(), 25);
	EXPECT_THROW(graphs.front()->validateSourceExport(1), std::invalid_argument);
}
