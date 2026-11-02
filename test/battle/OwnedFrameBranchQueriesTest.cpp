/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/OwnedFrameBranchQueries.h"
#include "../../lib/battle/OwnedUnitBonusFrameBatch.h"
#include "../../lib/bonuses/ProjectedBonusView.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/GameLibrary.h"
namespace
{
using Frame = battle::OwnedUnitBonusFrame;
using Bridge = battle::OwnedFrameBranchQueries;
class SeedInfo final : public battle::IUnitInfo
{
	const battle::OwnedUnitCreatureResolver & types;
public:
	explicit SeedInfo(const battle::OwnedUnitCreatureResolver & types) : types(types) {}
	int32_t unitBaseAmount() const override { return 2; }
	uint32_t unitId() const override { return 17; }
	BattleSide unitSide() const override { return BattleSide::ATTACKER; }
	PlayerColor unitOwner() const override { return PlayerColor(0); }
	SlotID unitSlot() const override { return SlotID(0); }
	const CCreature * unitType() const override { return types.creature(CreatureID(1)); }
	const battle::UnitCreatureResolver * unitCreatureResolver() const override { return &types; }
	std::optional<battle::UnitPreparationHealthBasis> unitPreparationHealthBasis() const override
	{
		return battle::UnitPreparationHealthBasis{20, 20};
	}
};
class SeedView final : public ProjectedBonusView
{
public:
	const battle::Unit * subject = nullptr;
	const battle::Unit * evaluatedUnit() const override { return subject; }
	void onBattleFormChanged(const battle::CUnitState &) const override {}
	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string &) const override
	{
		auto result = std::make_shared<BonusList>();
		auto health = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH, BonusSource::OTHER, 10, BonusSourceID());
		if(selector(health.get())) result->push_back(health);
		return result;
	}
	TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override { return getAllBonuses(selector, {}); }
	int32_t getTreeVersion() const override { return 0; }
};
Frame::Inputs inputs(uint32_t id = 17)
{
	Frame::Inputs inputs;
	inputs.identity = {2, id, BattleSide::ATTACKER, PlayerColor(0), SlotID(0), CreatureID(1), {20, 20}};
	battle::OwnedUnitCreatureResolver::Creature type;
	type.id = CreatureID(1); type.key = "owned:one"; type.faction = FactionID(0); type.level = 1;
	inputs.creatures = std::make_shared<battle::OwnedUnitCreatureResolver>(std::vector{type});
	OwnedRecipientBonusGraph::NodeInput root;
	root.facts.nodeType = BonusNodeType::STACK_BATTLE; root.facts.stackCount = 2; root.facts.stableStackLevel = 1;
	root.facts.acceptance.emplace(); root.facts.acceptance->metadata.slot = SlotID(0);
	root.facts.creatureContext.emplace(); root.facts.creatureContext->faction = FactionID(0);
	root.facts.creatureContext->creature = OwnedBonusEvaluationFacts::CreatureContext::Creature{CreatureID(1), 1, EAlignment::NEUTRAL};
	root.local.push_back(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH, BonusSource::OTHER, 10, BonusSourceID()));
	inputs.nodes.push_back(root);
	Frame::FormSources source; source.full.facts = root.facts; source.full.records = {0}; source.native = source.full;
	inputs.forms.emplace(CreatureID(1), source);
	for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		inputs.environment.colors.at(side) = PlayerColor(side == BattleSide::ATTACKER ? 0 : 1);
		inputs.environment.inputs.at(side).emplace(); inputs.environment.inputs.at(side)->color = *inputs.environment.colors.at(side);
		inputs.environment.states.at(side).emplace();
	}
	inputs.environment.magicRules.emplace(); inputs.environment.skeletonUpgrades.emplace();
	inputs.peerInputs.emplace(); inputs.recipientPosition = 0;
	SeedInfo info(*inputs.creatures); SeedView view; battle::CUnitStateDetached seed(&info, &view);
	view.subject = &seed; seed.localInit(nullptr); seed.position = BattleHex(50);
	inputs.state = seed.save();
	return inputs;
}
std::shared_ptr<Frame> frame() { return Frame::fromOwnedInputs(inputs()); }
std::vector<std::shared_ptr<Frame>> pair()
{
	using Batch = battle::OwnedUnitBonusFrameBatch;
	auto first = inputs(), second = inputs(18); second.creatures = first.creatures;
	second.nodes[0].local.push_back(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SPELL_RESISTANCE_AURA, BonusSource::OTHER, 25, BonusSourceID()));
	second.forms.at(CreatureID(1)).full.records.push_back(1);
	first.state["state"]["position"].Integer() = 93; second.state["state"]["position"].Integer() = 94;
	auto firstSeed = Frame::fromOwnedInputs(first), secondSeed = Frame::fromOwnedInputs(second);
	std::vector<OwnedRecipientBonusGraph::CaptureInput> graphs(2);
	graphs[0].nodes = first.nodes; graphs[1].nodes = second.nodes;
	graphs[1].sourceExports = {second.nodes[0].local.back()};
	std::vector<Batch::Recipe> recipes(2);
	for(size_t i = 0; i < 2; ++i)
	{
		auto & recipe = recipes[i]; auto & values = i == 0 ? first : second;
		recipe.baseGraph = i; recipe.inputs = values; recipe.inputs.nodes.clear(); recipe.inputs.peerInputs.reset();
		recipe.formGraphs.emplace(CreatureID(1), std::pair{i, i});
		const auto peer = 1 - i; const auto & peerValues = peer == 0 ? first : second;
		const auto & peerSeed = peer == 0 ? firstSeed : secondSeed;
		auto query = peerValues.forms.at(CreatureID(1)).full;
		query.facts.acceptance->state = {false, CreatureID(1), CreatureID(1), false, false, false};
		recipe.peers.push_back({peerValues.identity, peer, query, &peerSeed->unit(), {}});
		recipe.inputs.recipientPosition = i;
	}
	return Batch::fromOwnedInputs(std::move(graphs), std::move(recipes), OwnedBonusCaptureCodec::current());
}
struct WithoutLibrary
{
	GameLibrary * old = LIBRARY;
	WithoutLibrary() { LIBRARY = nullptr; }
	~WithoutLibrary() { LIBRARY = old; }
};
}
TEST(OwnedFrameBranchQueriesTest, CurrentScalarTransactionKeepsStableAddressAndIsolatesSibling)
{
	auto root = frame(); auto current = root->fork(); auto sibling = root->fork();
	auto scratch = current->scratch(); scratch.unit().position = BattleHex(51);
	const auto * address = &current->unit();
	WithoutLibrary absent;
	Bridge::synchronize(*current, {{current, &scratch.unit()}});
	EXPECT_EQ(&current->unit(), address); EXPECT_EQ(current->unit().getPosition(), BattleHex(51));
	EXPECT_EQ(root->unit().getPosition(), BattleHex(50)); EXPECT_EQ(sibling->unit().getPosition(), BattleHex(50));
	const auto before = current->save();
	EXPECT_THROW(Bridge::synchronize(*current, {{current, nullptr}, {current, nullptr}}), std::invalid_argument);
	EXPECT_EQ(current->save(), before);
}
TEST(OwnedFrameBranchQueriesTest, SourceRoleAndPendingClockRemainSeparateFromQueryRole)
{
	auto root = frame();
	WithoutLibrary absent;
	EXPECT_TRUE(Bridge::sourceExports(*root).empty()); // Fixture query health is NOT an explicit source export.
	Bonus clock(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	Bridge::append(*root, Bridge::prepareRecords({clock}), 0);
	auto child = root->fork(); auto retained = Bridge::acquireState(root);
	EXPECT_TRUE(root->unit().isTimeStopped()); EXPECT_TRUE(child->unit().isTimeStopped());
	Bridge::removeMatching(*child, Selector::type()(BonusType::TIME_STOP));
	EXPECT_FALSE(child->unit().isTimeStopped()); EXPECT_TRUE(root->unit().isTimeStopped());
	root.reset(); EXPECT_TRUE(retained->isTimeStopped());
}

TEST(OwnedFrameBranchQueriesTest, RefreshPreservesExportOrderAndForkRetainsNestedMetadataOwner)
{
	auto root = frame();
	WithoutLibrary absent;
	Bonus first(BonusDuration::N_TURNS, BonusType::LUCK, BonusSource::SPELL_EFFECT, 1, BonusSourceID());
	first.turnsRemain = 2;
	first.limiter = std::make_shared<AllOfLimiter>(std::vector<TLimiterPtr>{
		std::make_shared<HasAnotherBonusLimiter>(BonusType::STACK_HEALTH)});
	Bonus second(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	second.limiter = first.limiter;
	const auto prepared = Bridge::prepareRecords({first, second});
	Bridge::append(*root, prepared, 0); Bridge::append(*root, prepared, 1);
	first.turnsRemain = 4; Bridge::refresh(*root, first);
	const auto exports = Bridge::sourceExports(*root);
	ASSERT_EQ(exports.size(), 2u); EXPECT_EQ(exports[0]->type, BonusType::LUCK);
	EXPECT_EQ(exports[0]->limiter, exports[1]->limiter);
	EXPECT_EQ(exports[0]->turnsRemain, 4); EXPECT_EQ(exports[1]->type, BonusType::TIME_STOP);
	auto child = root->fork(); root.reset();
	const auto retained = Bridge::sourceExports(*child);
	ASSERT_EQ(retained.size(), 2u);
	const auto * aggregate = dynamic_cast<const AggregateLimiter *>(retained.front()->limiter.get());
	ASSERT_NE(aggregate, nullptr); EXPECT_EQ(aggregate->limiters.size(), 1u);
}

TEST(OwnedFrameBranchQueriesTest, CurrentPeerSourceLedgerAndHealthPublishAsOneRosterAndKeepFrozenLoan)
{
	auto frames = pair(); auto frozen = frames[0]->scratch();
	WithoutLibrary absent;
	EXPECT_EQ(frames[0]->unit().magicResistance(), 25);
	Bridge::removeMatching(*frames[1], Selector::type()(BonusType::SPELL_RESISTANCE_AURA));
	Bridge::synchronize(*frames[0], {{frames[0], nullptr}, {frames[1], nullptr}});
	EXPECT_EQ(frames[0]->unit().magicResistance(), 0); EXPECT_EQ(frozen.unit().magicResistance(), 25);
	const auto before0 = frames[0]->save(), before1 = frames[1]->save();
	EXPECT_THROW(Bridge::synchronize(*frames[0], {{frames[1], nullptr}, {frames[0], nullptr}}), std::invalid_argument);
	EXPECT_EQ(frames[0]->save(), before0); EXPECT_EQ(frames[1]->save(), before1);
	EXPECT_EQ(frames[0]->unit().magicResistance(), 0);
}
