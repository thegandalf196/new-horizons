/*
 * ProjectedFormTransitionTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../mock/mock_UnitEnvironment.h"
#include "../mock/mock_UnitInfo.h"
#include "../../lib/CStack.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/UnitStateValueFields.h"
#include "../../lib/bonuses/ProjectedBonusGraph.h"
#include "../../lib/bonuses/ProjectedBonusRoster.h"
#include "../../lib/bonuses/PreparedBonusGraphFinalization.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include <stdexcept>
#include <type_traits>

namespace test
{
using namespace ::testing;
using BoundLoad = decltype(&ProjectedBonusGraph::loadBoundState);
static_assert(std::is_invocable_v<BoundLoad, ProjectedBonusGraph &, const CStack &,
	battle::CUnitStateDetached &, const JsonNode &>);
static_assert(!std::is_invocable_v<BoundLoad, ProjectedBonusGraph &, const CStack &,
	CStack &, const JsonNode &>); // live-world mutation is not this API

class ProjectedFormTransitionTest : public Test
{
protected:
	CStackBasicDescriptor descriptor{CreatureID(0), 8};
	CStack original{&descriptor, PlayerColor(0), 17, BattleSide::ATTACKER};
	CStackInstance base{nullptr, CreatureID(0), 8, true};
	UnitInfoMock info;
	UnitEnvironmentMock environment;
	ProjectedBonusGraph graph;
	std::unique_ptr<battle::CUnitStateDetached> unit;
	std::unique_ptr<ProjectedBonusRoster> roster;

	void SetUp() override
	{
		EXPECT_CALL(info, unitBaseAmount()).WillRepeatedly(Return(8));
		EXPECT_CALL(info, unitId()).WillRepeatedly(Return(17));
		EXPECT_CALL(info, unitType()).WillRepeatedly(Return(CreatureID(0).toCreature()));
		EXPECT_CALL(info, unitOwner()).WillRepeatedly(Return(PlayerColor(0)));
		EXPECT_CALL(info, unitSide()).WillRepeatedly(Return(BattleSide::ATTACKER));
		EXPECT_CALL(info, unitSlot()).WillRepeatedly(Return(original.unitSlot()));
		EXPECT_CALL(environment, unitHasAmmoCart(_)).WillRepeatedly(Return(false));
		auto health = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH,
			BonusSource::CREATURE_ABILITY, 20, BonusSourceID(CreatureID(0)));
		original.addNewBonus(health);
		original.base = &base; // defined NONE terrain, not an unbound battle fallback
		original.health.init();
		original.position = BattleHex(5, 5);
	}

	void initialize()
	{
		unit = std::make_unique<battle::CUnitStateDetached>(&info, &graph.view(original));
		battle::UnitStateValueFields::copy(*unit, original); // initialization, NOT an event
		unit->bindStateEnvironment(&environment);
		roster = std::make_unique<ProjectedBonusRoster>(std::vector<const battle::Unit *>{unit.get()});
		graph.bind(original, *unit, *roster);
	}

	TNodesVector fixtureAuthority()
	{
		TNodesVector result;
		// The single-threaded test library owns these mutable prototypes. This
		// test-only cast is NOT a production world lease or mutation-authority API.
		for(const auto * node : graph.capturedNodes())
			result.push_back(const_cast<CBonusSystemNode *>(node));
		return result;
	}
};

TEST_F(ProjectedFormTransitionTest, OrdinaryDetachedCopyRetainsLegacyHookWithoutProjectedBinding)
{
	original.beginBattleForm(CreatureID(2), 3);
	const auto version = original.getTreeVersion();
	battle::CUnitStateDetached ordinary(&info, &original);
	static_cast<battle::CUnitState &>(ordinary) = original;
	EXPECT_EQ(ordinary.getBattleFormViewRevision(), 1);
	EXPECT_EQ(ordinary.battleFormCreature(), CreatureID(2));
	EXPECT_EQ(original.getTreeVersion(), version);
}

TEST_F(ProjectedFormTransitionTest, SameActiveLoadRetainsActualLiveResetReattachBeforeHealth)
{
	original.beginBattleForm(CreatureID(2), 3);
	const auto data = original.save();
	const auto input = data;
	const auto liveBefore = uint32_t(PreparedBonusGraphStorage::currentGeneration());
	original.load(data);
	const auto liveDelta = uint32_t(PreparedBonusGraphStorage::currentGeneration()) - liveBefore;
	ASSERT_EQ(liveDelta, 4); // detach+hook, attach+hook, despite equal final form
	ASSERT_EQ(original.save(), data);
	initialize();
	const auto before = uint32_t(PreparedBonusGraphStorage::currentGeneration());
	const auto parents = original.getParentNodes();
	graph.loadBoundState(original, *unit, data);
	EXPECT_EQ(unit->getBattleFormViewRevision(), 2);
	EXPECT_EQ(unit->save(), data);
	EXPECT_EQ(graph.parents(original), parents);
	EXPECT_EQ(original.save(), data);
	EXPECT_EQ(data, input);
	PreparedBonusGraphFinalization plan(graph, fixtureAuthority());
	ASSERT_TRUE(plan.commit()); // final graph/value already identical, not a world form commit
	EXPECT_EQ(uint32_t(PreparedBonusGraphStorage::currentGeneration()) - before, liveDelta);
	EXPECT_EQ(original.save(), data);
}

TEST_F(ProjectedFormTransitionTest, UnchangedSourceNotificationMatchesUnconditionalLiveInvalidation)
{
	const auto liveBefore = uint32_t(PreparedBonusGraphStorage::currentGeneration());
	static_cast<battle::CUnitState &>(original).onBattleFormChanged();
	const auto liveDelta = uint32_t(PreparedBonusGraphStorage::currentGeneration()) - liveBefore;
	ASSERT_EQ(liveDelta, 1);
	initialize();
	const auto before = uint32_t(PreparedBonusGraphStorage::currentGeneration());
	unit->onBattleFormChanged();
	EXPECT_EQ(unit->getBattleFormViewRevision(), 1);
	PreparedBonusGraphFinalization plan(graph, fixtureAuthority());
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(uint32_t(PreparedBonusGraphStorage::currentGeneration()) - before, liveDelta);
}

TEST_F(ProjectedFormTransitionTest, DifferentActiveFormLoadsCanonicalLiveHealthWithoutLiveWrites)
{
	original.beginBattleForm(CreatureID(2), 3);
	const auto initial = original.save();
	original.endBattleForm();
	original.beginBattleForm(CreatureID(4), 2);
	const auto incoming = original.save();
	original.load(initial);
	initialize();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	graph.loadBoundState(original, *unit, incoming);
	EXPECT_EQ(unit->save(), incoming);
	EXPECT_TRUE(vstd::contains(graph.parents(original), CreatureID(4).toCreature()));
	EXPECT_FALSE(vstd::contains(graph.parents(original), CreatureID(2).toCreature()));
	EXPECT_EQ(original.save(), initial);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	// Do not graph-only commit a different form: world value/source scalars are not staged here.
}

TEST_F(ProjectedFormTransitionTest, LateGuardianFailurePoisonsPreparationAfterResetHookOnly)
{
	original.beginBattleForm(CreatureID(2), 3);
	const auto before = original.save();
	const auto parents = original.getParentNodes();
	initialize();
	auto invalid = before;
	invalid["state"]["guardianSpiritHitPoints"].Integer() = -1;
	const auto input = invalid;
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_THROW(graph.loadBoundState(original, *unit, invalid), std::runtime_error);
	EXPECT_EQ(unit->getBattleFormViewRevision(), 1); // reset hook precedes this field
	EXPECT_THROW(graph.query(original, Selector::all, false), std::logic_error);
	EXPECT_THROW((PreparedBonusGraphFinalization(graph, {})), std::logic_error);
	EXPECT_EQ(original.save(), before);
	EXPECT_EQ(original.getParentNodes(), parents);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_EQ(invalid, input);
}

TEST_F(ProjectedFormTransitionTest, PreloadTypeFailurePoisonsWithoutInventingAFormEvent)
{
	original.beginBattleForm(CreatureID(2), 3);
	const auto before = original.save();
	initialize();
	auto invalid = before;
	invalid["state"]["luckyOwnAttackSequence"].String() = "invalid";
	EXPECT_THROW(graph.loadBoundState(original, *unit, invalid), std::runtime_error);
	EXPECT_EQ(unit->getBattleFormViewRevision(), 0);
	EXPECT_THROW(graph.query(original, Selector::all, false), std::logic_error);
	EXPECT_EQ(original.save(), before);
}

TEST_F(ProjectedFormTransitionTest, UnboundProjectedNotificationCannotSilentlyDropAnEvent)
{
	battle::CUnitStateDetached unbound(&info, &graph.view(original));
	battle::UnitStateValueFields::copy(unbound, original);
	const auto before = original.save();
	EXPECT_THROW(unbound.onBattleFormChanged(), std::logic_error);
	EXPECT_THROW(graph.query(original, Selector::all, false), std::logic_error);
	EXPECT_EQ(original.save(), before);
}

TEST_F(ProjectedFormTransitionTest, WrongBoundOwnerRefusesBeforeParsingOrPrivateStateMutation)
{
	initialize();
	battle::CUnitStateDetached other(&info, &graph.view(original));
	battle::UnitStateValueFields::copy(other, original);
	JsonNode invalid;
	invalid.String() = "not a state";
	const auto before = original.save();
	EXPECT_THROW(graph.loadBoundState(original, other, invalid), std::logic_error);
	EXPECT_EQ(other.getBattleFormViewRevision(), 0);
	EXPECT_EQ(other.getPosition(), original.getPosition());
	EXPECT_EQ(original.save(), before);
}

TEST_F(ProjectedFormTransitionTest, OrdinaryBonusProviderCannotBindOrBypassSynchronousLoad)
{
	original.beginBattleForm(CreatureID(2), 3);
	const auto before = original.save();
	(void)graph.view(original); // presence alone is not provider ownership
	battle::CUnitStateDetached ordinary(&info, &original);
	battle::UnitStateValueFields::copy(ordinary, original);
	ordinary.bindStateEnvironment(&environment);
	ProjectedBonusRoster localRoster({&ordinary});
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_THROW(graph.bind(original, ordinary, localRoster), std::invalid_argument);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_THROW(graph.loadBoundState(original, ordinary, before), std::logic_error);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_EQ(ordinary.getBattleFormViewRevision(), 0);
	EXPECT_EQ(ordinary.save(), before); // ordinary nonprojected reads remain valid
	EXPECT_EQ(original.save(), before);
}

TEST_F(ProjectedFormTransitionTest, ForeignGraphProviderCannotBindSameStateIntoThisOwner)
{
	original.beginBattleForm(CreatureID(2), 3);
	const auto before = original.save();
	ProjectedBonusGraph foreign;
	battle::CUnitStateDetached state(&info, &foreign.view(original));
	battle::UnitStateValueFields::copy(state, original);
	state.bindStateEnvironment(&environment);
	ProjectedBonusRoster localRoster({&state});
	foreign.bind(original, state, localRoster); // exact-provider positive counterpart
	(void)graph.view(original);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_THROW(graph.bind(original, state, localRoster), std::invalid_argument);
	EXPECT_THROW(graph.loadBoundState(original, state, before), std::logic_error);
	EXPECT_EQ(state.getBattleFormViewRevision(), 0);
	EXPECT_EQ(state.save(), before); // no notifications or poisoning reached foreign
	EXPECT_EQ(original.save(), before);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(ProjectedFormTransitionTest, ProviderReplacementAfterValidBindingRefusesBeforeParsing)
{
	initialize();
	battle::CUnitStateDetached ordinary(&info, &original);
	battle::UnitStateValueFields::copy(ordinary, original);
	*unit = ordinary; // deliberately exercise public same-type pointer replacement
	const auto before = unit->save();
	JsonNode invalid;
	invalid.String() = "must not be parsed";
	EXPECT_THROW(graph.loadBoundState(original, *unit, invalid), std::logic_error);
	EXPECT_EQ(unit->save(), before);
	EXPECT_EQ(unit->getBattleFormViewRevision(), 0);
	EXPECT_EQ(original.save(), before);
}
}
