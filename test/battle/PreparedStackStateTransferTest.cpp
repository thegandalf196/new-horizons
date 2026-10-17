/*
 * PreparedStackStateTransferTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../mock/mock_UnitEnvironment.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/PreparedStackStateTransfer.h"
#include "../../lib/battle/UnitStateValueFields.h"
#include "../../lib/bonuses/ProjectedBonusGraph.h"
#include "../../lib/bonuses/ProjectedBonusView.h"
#include "../../lib/bonuses/ProjectedBonusRoster.h"
#include "../../lib/bonuses/PreparedBonusGraphFinalization.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include <stdexcept>
#include <limits>
#include <type_traits>

namespace test
{
using namespace ::testing;
static_assert(!std::is_copy_constructible_v<battle::PreparedStackStateTransfer>);
static_assert(!std::is_move_constructible_v<battle::PreparedStackStateTransfer>);
// Test-only JSON observation under EXACT accepted pre-hook bonus inputs.
// Never a forecast/provider fallback: the original stale view must still refuse.
class FrozenSerializationView final : public ProjectedBonusView
{
	TConstBonusListPtr stacked, unstacked;
	battle::CUnitStateDetached observed;
	static TConstBonusListPtr freeze(const TConstBonusListPtr & source)
	{
		auto result = std::make_shared<BonusList>();
		for(const auto & bonus : *source)
			result->push_back(std::make_shared<Bonus>(*bonus));
		return result;
	}
	static TConstBonusListPtr select(const BonusList & source, const CSelector & selector)
	{
		auto result = std::make_shared<BonusList>();
		source.getBonuses(*result, selector);
		return result;
	}
public:
	FrozenSerializationView(const battle::CUnitState & source, const battle::IUnitInfo & identity)
		: stacked(freeze(source.getAllBonuses(Selector::all))),
		unstacked(freeze(source.getUnstackedBonuses(Selector::all))), observed(&identity, this) {}
	const battle::Unit * evaluatedUnit() const override { return &observed; }
	void onBattleFormChanged(const battle::CUnitState &) const override
	{
		throw std::logic_error("A frozen serialization observer must not run form hooks");
	}
	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string &) const override
	{
		return select(*stacked, selector);
	}
	TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override
	{
		return select(*unstacked, selector);
	}
	int32_t getTreeVersion() const override { return 0; }
	JsonNode observe(const battle::CUnitState & values)
	{
		battle::UnitStateValueFields::copy(observed, values); // All81 values, no view/cache/owner copy.
		return observed.save();
	}
};

class PreparedStackStateTransferTest : public Test
{
protected:
	CStackBasicDescriptor descriptor{CreatureID(0), 8};
	CStackInstance base{nullptr, CreatureID(0), 8, true};
	CStack original{&descriptor, PlayerColor(0), 17, BattleSide::ATTACKER};
	CStack scalar{&descriptor, PlayerColor(0), 17, BattleSide::ATTACKER};
	UnitEnvironmentMock environment;
	ProjectedBonusGraph graph;
	std::unique_ptr<battle::CUnitStateDetached> unit;
	std::unique_ptr<ProjectedBonusRoster> roster;

	void SetUp() override
	{
		EXPECT_CALL(environment, unitHasAmmoCart(_)).WillRepeatedly(Return(false));
		for(auto * stack : {&original, &scalar})
		{
			stack->base = &base; // Defined adventure NONE, no invented battle membership.
			stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, 20, BonusSourceID(CreatureID(0))));
			stack->health.init();
			stack->position = BattleHex(5, 5);
		}
	}
	JsonNode incoming(CreatureID creature)
	{
		const auto before = scalar.save();
		scalar.beginBattleForm(creature, 3);
		const auto result = scalar.save();
		scalar.load(before);
		return result;
	}
	void initialize()
	{
		unit = std::make_unique<battle::CUnitStateDetached>(&original, &graph.view(original));
		battle::UnitStateValueFields::copy(*unit, original);
		unit->bindStateEnvironment(&environment);
		roster = std::make_unique<ProjectedBonusRoster>(std::vector<const battle::Unit *>{unit.get()});
		graph.bind(original, *unit, *roster);
	}
	TNodesVector authority()
	{
		TNodesVector result;
		// Only this single-threaded fixture owns mutable library prototypes.
		// This cast is not a production authority/lease acquisition mechanism.
		for(const auto * node : graph.capturedNodes())
			result.push_back(const_cast<CBonusSystemNode *>(node));
		return result;
	}
	void checkTransfer(const JsonNode & data, bool requireCapacityChange = false)
	{
		const auto old = original.save();
		const auto originalRevision = original.getBattleFormViewRevision();
		const auto scalarRevision = scalar.getBattleFormViewRevision();
		scalar.load(data); // Actual live hook oracle BEFORE graph capture.
		const auto revisionDelta = scalar.getBattleFormViewRevision() - scalarRevision;
		initialize();
		const auto oldMaximum = original.getMaxHealth(); // Deliberately warm live HEALTH cache.
		if(requireCapacityChange)
		{
			ASSERT_NE(oldMaximum, scalar.getMaxHealth());
		}
		const auto before = PreparedBonusGraphStorage::currentGeneration();
		graph.loadBoundState(original, *unit, data);
		battle::PreparedStackStateTransfer transfer(original, *unit, graph);
		EXPECT_EQ(original.save(), old);
		EXPECT_EQ(original.getMaxHealth(), oldMaximum);
		EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), before);
		ASSERT_TRUE(transfer.readyToCommit());
		PreparedBonusGraphFinalization finalizer(graph, authority());
		ASSERT_TRUE(finalizer.commit());
		const auto afterGraph = PreparedBonusGraphStorage::currentGeneration();
		static_assert(noexcept(transfer.commit()));
		ASSERT_TRUE(transfer.commit());
		EXPECT_EQ(original.save(), scalar.save());
		EXPECT_EQ(original.save(), data);
		EXPECT_EQ(original.getMaxHealth(), scalar.getMaxHealth());
		EXPECT_EQ(original.unitType(), scalar.unitType());
		EXPECT_EQ(original.doubleWide(), scalar.doubleWide());
		EXPECT_EQ(original.getBattleFormViewRevision(), originalRevision + revisionDelta);
		EXPECT_EQ(original.unitId(), 17u);
		// Finalization closes private queries; inspect only raw transferred flags.
		EXPECT_EQ(unit->defending, old["state"]["defending"].Bool());
		EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), afterGraph);
		EXPECT_FALSE(transfer.readyToCommit());
		EXPECT_FALSE(transfer.commit());
		EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), afterGraph);
	}
};

TEST_F(PreparedStackStateTransferTest, CanonicalToFormPreservesLiveJsonAndWarmHealthCache)
{
	const auto data = incoming(CreatureID(2));
	checkTransfer(data, true);
}

TEST_F(PreparedStackStateTransferTest, ActiveToDifferentFormUsesLiveSourceAndFootprint)
{
	CreatureID target(-1);
	for(const auto & creature : LIBRARY->creh->objects)
		if(creature && creature->isDoubleWide() != CreatureID(2).toCreature()->isDoubleWide())
		{
			target = creature->getId();
			break;
		}
	ASSERT_TRUE(target.hasValue());
	const auto data = incoming(target);
	original.beginBattleForm(CreatureID(2), 3);
	scalar.beginBattleForm(CreatureID(2), 3);
	checkTransfer(data);
	// A later ordinary hook must detach the NEW source, not the previous form.
	original.endBattleForm();
	EXPECT_FALSE(vstd::contains(original.getParentNodes(), target.toCreature()));
	EXPECT_FALSE(vstd::contains(original.getParentNodes(), CreatureID(2).toCreature()));
}

TEST_F(PreparedStackStateTransferTest, ActiveToCanonicalRetiresSourceWithoutReplayingHook)
{
	const auto data = original.save();
	original.beginBattleForm(CreatureID(2), 3);
	scalar.beginBattleForm(CreatureID(2), 3);
	checkTransfer(data);
	EXPECT_FALSE(vstd::contains(original.getParentNodes(), CreatureID(2).toCreature()));
}

TEST_F(PreparedStackStateTransferTest, SameFinalFormRetainsResetReattachRevisionAndNoExtraEpoch)
{
	original.beginBattleForm(CreatureID(2), 3);
	scalar.beginBattleForm(CreatureID(2), 3);
	checkTransfer(original.save());
}

TEST_F(PreparedStackStateTransferTest, ChangedLiveRevisionRefusesBeforeAnyValuesOrViewWrite)
{
	const auto data = incoming(CreatureID(2));
	initialize();
	graph.loadBoundState(original, *unit, data);
	battle::PreparedStackStateTransfer transfer(original, *unit, graph);
	const auto projected = unit->save();
	FrozenSerializationView frozen(*unit, original);
	ASSERT_EQ(frozen.observe(*unit), projected); // Establish exact JSON observation before staleness.
	const auto projectedRevision = unit->getBattleFormViewRevision();
	const auto originalRevision = original.getBattleFormViewRevision();
	static_cast<battle::CUnitState &>(original).onBattleFormChanged();
	ASSERT_EQ(original.getBattleFormViewRevision(), originalRevision + 1);
	const auto live = original.save();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_FALSE(transfer.readyToCommit());
	EXPECT_FALSE(transfer.commit());
	EXPECT_EQ(original.save(), live);
	EXPECT_THROW(unit->save(), std::logic_error); // The actual stale graph is NEVER queried as an oracle.
	EXPECT_EQ(frozen.observe(*unit), projected); // Same complete value JSON under identical captured inputs.
	EXPECT_EQ(unit->getBattleFormViewRevision(), projectedRevision);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(PreparedStackStateTransferTest, ChangedProjectedFormRefusesBeforeLiveWrites)
{
	const auto first = incoming(CreatureID(2)), later = incoming(CreatureID(4));
	initialize();
	graph.loadBoundState(original, *unit, first);
	battle::PreparedStackStateTransfer transfer(original, *unit, graph);
	graph.loadBoundState(original, *unit, later);
	const auto live = original.save(), projected = unit->save();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_FALSE(transfer.commit());
	EXPECT_EQ(original.save(), live);
	EXPECT_EQ(unit->save(), projected);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(PreparedStackStateTransferTest, OrdinaryAndForeignGraphProvidersAreRejectedWithoutMutation)
{
	initialize();
	battle::CUnitStateDetached ordinary(&original, &original);
	battle::UnitStateValueFields::copy(ordinary, original);
	ProjectedBonusGraph other;
	battle::CUnitStateDetached foreign(&original, &other.view(original));
	battle::UnitStateValueFields::copy(foreign, original);
	const auto before = original.save();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_THROW((battle::PreparedStackStateTransfer(original, ordinary, graph)), std::exception);
	EXPECT_THROW((battle::PreparedStackStateTransfer(original, foreign, graph)), std::exception);
	EXPECT_EQ(original.save(), before);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(PreparedStackStateTransferTest, EqualNumericIdDoesNotAuthorizeAnotherPhysicalDescriptor)
{
	battle::CUnitStateDetached wrong(&scalar, &graph.view(original));
	battle::UnitStateValueFields::copy(wrong, original);
	ProjectedBonusRoster wrongRoster(std::vector<const battle::Unit *>{&wrong});
	graph.bind(original, wrong, wrongRoster); // legacy unique-ID binding permits this shape
	const auto before = scalar.save();
	EXPECT_THROW((battle::PreparedStackStateTransfer(original, wrong, graph)), std::invalid_argument);
	EXPECT_THROW((battle::PreparedStackStateTransfer(scalar, wrong, graph)), std::invalid_argument);
	EXPECT_EQ(scalar.save(), before);
}

TEST_F(PreparedStackStateTransferTest, ChangedProviderAfterPreparationRefusesBeforeLiveWrites)
{
	initialize();
	battle::PreparedStackStateTransfer transfer(original, *unit, graph);
	battle::CUnitStateDetached ordinary(&original, &original);
	battle::UnitStateValueFields::copy(ordinary, original);
	*unit = ordinary; // same-type assignment also replaces private descriptor/provider pointers
	const auto before = original.save();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_FALSE(transfer.readyToCommit());
	EXPECT_FALSE(transfer.commit());
	EXPECT_EQ(original.save(), before);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(PreparedStackStateTransferTest, RetainedSourcesSurvivePlanUntilRegistryOwnedTeardown)
{
	const auto data = incoming(CreatureID(2));
	initialize();
	graph.loadBoundState(original, *unit, data);
	const auto & source = LIBRARY->creh->objects.at(2);
	const auto prior = source.use_count();
	const auto before = PreparedBonusGraphStorage::currentGeneration();
	int32_t afterCommit = 0;
	{
		battle::PreparedStackStateTransfer transfer(original, *unit, graph);
		EXPECT_GT(source.use_count(), prior);
		PreparedBonusGraphFinalization finalizer(graph, authority());
		ASSERT_TRUE(finalizer.commit());
		ASSERT_TRUE(transfer.commit());
		const auto after = PreparedBonusGraphStorage::currentGeneration();
		afterCommit = after;
		EXPECT_EQ(original.save(), data);
		EXPECT_GT(source.use_count(), prior);
		EXPECT_GT(uint32_t(after) - uint32_t(before), 0u);
	}
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), afterCommit);
	EXPECT_EQ(source.use_count(), prior);
	EXPECT_EQ(original.save(), data);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), afterCommit);
}

TEST_F(PreparedStackStateTransferTest, PreparedRevisionBeginsAtBindingNotAssumedZero)
{
	const auto data = incoming(CreatureID(2));
	unit = std::make_unique<battle::CUnitStateDetached>(&original, &graph.view(original));
	battle::UnitStateValueFields::copy(*unit, original);
	unit->bindStateEnvironment(&environment);
	// Explicit base-only private view history, not a graph notification/event.
	// Binding must capture this starting revision instead of assuming zero.
	unit->battle::CUnitState::onBattleFormChanged();
	roster = std::make_unique<ProjectedBonusRoster>(std::vector<const battle::Unit *>{unit.get()});
	graph.bind(original, *unit, *roster);
	const auto originalRevision = original.getBattleFormViewRevision();
	const auto boundRevision = unit->getBattleFormViewRevision();
	graph.loadBoundState(original, *unit, data);
	const auto delta = unit->getBattleFormViewRevision() - boundRevision;
	battle::PreparedStackStateTransfer transfer(original, *unit, graph);
	PreparedBonusGraphFinalization finalizer(graph, authority());
	ASSERT_TRUE(finalizer.commit());
	ASSERT_TRUE(transfer.commit());
	EXPECT_EQ(original.getBattleFormViewRevision(), originalRevision + delta);
	EXPECT_EQ(original.save(), data);
}

TEST_F(PreparedStackStateTransferTest, PrivateRevisionRollbackIsRejectedBeforeLiveMutation)
{
	unit = std::make_unique<battle::CUnitStateDetached>(&original, &graph.view(original));
	battle::UnitStateValueFields::copy(*unit, original);
	battle::CUnitStateDetached earlier(&original, &graph.view(original));
	battle::UnitStateValueFields::copy(earlier, original);
	unit->battle::CUnitState::onBattleFormChanged();
	roster = std::make_unique<ProjectedBonusRoster>(std::vector<const battle::Unit *>{unit.get()});
	graph.bind(original, *unit, *roster);
	const auto boundRevision = unit->getBattleFormViewRevision();
	const auto earlierRevision = earlier.getBattleFormViewRevision();
	ASSERT_LT(earlierRevision, boundRevision);
	*unit = earlier; // Assignment retains this object's view revision; it cannot create rollback.
	ASSERT_EQ(unit->getBattleFormViewRevision(), boundRevision);
	const auto before = original.save();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_THROW((battle::PreparedStackStateTransfer::checkedFinalRevision(
		original.getBattleFormViewRevision(), boundRevision, earlierRevision)), std::invalid_argument);
	EXPECT_EQ(original.save(), before);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(PreparedStackStateTransferTest, AssignmentRetainsBoundRevisionAndRealConstructorUsesZeroDelta)
{
	unit = std::make_unique<battle::CUnitStateDetached>(&original, &graph.view(original));
	battle::UnitStateValueFields::copy(*unit, original);
	battle::CUnitStateDetached earlier(&original, &graph.view(original));
	battle::UnitStateValueFields::copy(earlier, original);
	unit->battle::CUnitState::onBattleFormChanged();
	roster = std::make_unique<ProjectedBonusRoster>(std::vector<const battle::Unit *>{unit.get()});
	graph.bind(original, *unit, *roster);
	const auto boundRevision = unit->getBattleFormViewRevision();
	const auto originalRevision = original.getBattleFormViewRevision();
	const auto before = original.save();
	ASSERT_LT(earlier.getBattleFormViewRevision(), boundRevision);
	*unit = earlier;
	ASSERT_EQ(unit->getBattleFormViewRevision(), boundRevision);
	ASSERT_TRUE(unit->usesUnitInfo(original));
	ASSERT_TRUE(unit->usesBonusProvider(graph.view(original)));
	battle::PreparedStackStateTransfer transfer(original, *unit, graph);
	ASSERT_TRUE(transfer.readyToCommit());
	PreparedBonusGraphFinalization finalizer(graph, authority());
	ASSERT_TRUE(finalizer.commit());
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	ASSERT_TRUE(transfer.commit());
	EXPECT_EQ(original.getBattleFormViewRevision(), originalRevision);
	EXPECT_EQ(unit->getBattleFormViewRevision(), boundRevision);
	EXPECT_EQ(original.save(), before);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(PreparedStackStateTransferTest, CheckedRevisionUsesWideArithmeticAndPreservesExactBoundaryErrors)
{
	using Transfer = battle::PreparedStackStateTransfer;
	const auto low = std::numeric_limits<int32_t>::min(), high = std::numeric_limits<int32_t>::max();
	const auto before = original.save();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_EQ(Transfer::checkedFinalRevision(low, 0, 0), low);
	EXPECT_EQ(Transfer::checkedFinalRevision(high, 1, 1), high);
	EXPECT_EQ(Transfer::checkedFinalRevision(low, low, high), high);
	EXPECT_THROW((Transfer::checkedFinalRevision(high, 1, 0)), std::invalid_argument);
	EXPECT_THROW((Transfer::checkedFinalRevision(high, 0, 1)), std::overflow_error);
	EXPECT_THROW((Transfer::checkedFinalRevision(high, low, high)), std::overflow_error);
	EXPECT_EQ(original.save(), before);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}
}
