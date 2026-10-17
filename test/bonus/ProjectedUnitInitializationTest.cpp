/*
 * ProjectedUnitInitializationTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../mock/mock_BonusBearer.h"
#include "../mock/mock_UnitEnvironment.h"
#include "../../lib/bonuses/ProjectedBonusGraph.h"
#include "../../lib/bonuses/ProjectedBonusRoster.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/bonuses/BonusEvaluationContext.h"
#include "../../lib/battle/StackInitializationReceipts.h"
#include "../../lib/battle/UnitStateValueFields.h"
#include "../../lib/CStack.h"
#include "../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../lib/json/JsonNode.h"
#include <stdexcept>

namespace test
{
using namespace ::testing;
namespace
{
std::shared_ptr<Bonus> initializationBonus(BonusType type, int value)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, type, BonusSource::OTHER, value, BonusSourceID());
}

class RejectingInitializationUpdater final : public IUpdater
{
public:
	std::shared_ptr<Bonus> createUpdatedBonus(const std::shared_ptr<Bonus> &,
		const BonusEvaluationContext &) const override
	{
		throw std::runtime_error("synthetic initialization query failure");
	}
};

class SummonSensitiveInitializationUpdater final : public IUpdater
{
public:
	std::shared_ptr<Bonus> createUpdatedBonus(const std::shared_ptr<Bonus> & bonus,
		const BonusEvaluationContext & context) const override
	{
		auto result = std::make_shared<Bonus>(*bonus);
		result->val = dynamic_cast<const battle::CUnitState &>(*context.battleUnit()).summoned ? 100 : 0;
		return result;
	}
};
}

class ProjectedUnitInitializationTest : public Test
{
protected:
	CStackBasicDescriptor firstDescriptor{CreatureID(0), 8};
	CStackBasicDescriptor secondDescriptor{CreatureID(0), 3};
	CStack first{&firstDescriptor, PlayerColor(0), 17, BattleSide::ATTACKER};
	CStack second{&secondDescriptor, PlayerColor(0), 17, BattleSide::ATTACKER};
	ProjectedBonusGraph graph;
	battle::CUnitStateDetached one{&first, &graph.view(first)};
	battle::CUnitStateDetached two{&second, &graph.view(second)};
	UnitEnvironmentMock environment;

	void SetUp() override
	{
		EXPECT_CALL(environment, unitHasAmmoCart(_)).WillRepeatedly(Return(false));
		battle::UnitStateValueFields::copy(one, first);
		battle::UnitStateValueFields::copy(two, second);
	}

	void initialize(ProjectedBonusRoster & roster)
	{
		graph.bindOccurrence(first, one, roster);
		graph.bindOccurrence(second, two, roster);
		for(auto * node : {&first, &second})
			graph.addExported(*node, initializationBonus(BonusType::STACK_HEALTH, 20));
	}

	void seedReceipts()
	{
		one.personalBloodrageIncrement = 7;
		one.waiting = one.waitedThisTurn = one.movedThisRound = true;
		one.hadMorale = one.moraleExtraActivation = true;
		one.battlecraftWaitBonusUsed = one.battlecraftWaitMasteryDoubled = true;
		one.veteranCohesionEarned = true;
		one.confusionState.applyPending(PlayerColor(0), true);
		one.battlecraftOverwatchReadyRound = 2;
		one.battlecraftOverwatchUsedRound = 3;
		one.heroicSpiritRetaliation = one.heroicSpiritMoralePending = true;
		one.pursuitMovementRemaining = 4;
		one.luckyOwnAttackSequence = true;
		one.restoreFrozenApplicationRound(5);
		one.timeStopTurnConsumedFlag = true;
		one.veteranPhysicalDamageSinceActivation = 11;
		one.noQuarterMoraleActivationsRemaining = 2;
	}
};

TEST_F(ProjectedUnitInitializationTest, DefaultRosterStillRejectsEqualPacketIds)
{
	EXPECT_THROW((ProjectedBonusRoster({&one, &two})), std::invalid_argument);
}

TEST_F(ProjectedUnitInitializationTest, OccurrenceRosterRetainsPhysicalOrderAndFirstMatchEvenWhenGhost)
{
	ProjectedBonusRoster roster({&one, &two}, ProjectedBonusRoster::Identity::OrderedOccurrences);
	one.ghost = true;
	ASSERT_EQ(roster.orderedUnits().size(), 2);
	EXPECT_EQ(roster.orderedUnits()[0], &one);
	EXPECT_EQ(roster.orderedUnits()[1], &two);
	EXPECT_EQ(roster.firstById(17), &one);
	EXPECT_EQ(roster.firstById(99), nullptr);
}

TEST_F(ProjectedUnitInitializationTest, OccurrenceRosterRejectsNullAndRepeatedPhysicalState)
{
	EXPECT_THROW((ProjectedBonusRoster({&one, nullptr}, ProjectedBonusRoster::Identity::OrderedOccurrences)), std::invalid_argument);
	EXPECT_THROW((ProjectedBonusRoster({&one, &one}, ProjectedBonusRoster::Identity::OrderedOccurrences)), std::invalid_argument);
}

TEST_F(ProjectedUnitInitializationTest, OrdinaryBindDoesNotImplicitlyOptIntoOccurrenceIdentity)
{
	ProjectedBonusRoster roster({&one, &two}, ProjectedBonusRoster::Identity::OrderedOccurrences);
	graph.bind(first, one, roster);
	EXPECT_THROW(graph.bind(second, two, roster), std::invalid_argument);
	graph.requireBoundUnit(first, one);
}

TEST_F(ProjectedUnitInitializationTest, OccurrenceBindRequiresExplicitRosterAndExactGraphView)
{
	ProjectedBonusRoster strict({&one});
	EXPECT_THROW(graph.bindOccurrence(first, one, strict), std::invalid_argument);
	battle::CUnitStateDetached ordinary(&first, &first);
	ProjectedBonusRoster ordered({&ordinary}, ProjectedBonusRoster::Identity::OrderedOccurrences);
	EXPECT_THROW(graph.bindOccurrence(first, ordinary, ordered), std::invalid_argument);
}

TEST_F(ProjectedUnitInitializationTest, EqualIdsAndCorrectViewCannotMaskWrongPhysicalDescriptor)
{
	battle::CUnitStateDetached wrong(&second, &graph.view(first));
	ProjectedBonusRoster roster({&wrong}, ProjectedBonusRoster::Identity::OrderedOccurrences);
	EXPECT_THROW(graph.bindOccurrence(first, wrong, roster), std::invalid_argument);
	EXPECT_TRUE(liveBonusGraph().parents(first).empty());
}

TEST_F(ProjectedUnitInitializationTest, DescriptorReplacementAfterBindingRefusesBeforeLoadOrNotification)
{
	ProjectedBonusRoster roster({&one}, ProjectedBonusRoster::Identity::OrderedOccurrences);
	graph.bindOccurrence(first, one, roster);
	battle::CUnitStateDetached wrong(&second, &graph.view(first));
	one = wrong; // deliberate same-ID descriptor replacement through legacy value operation
	one.personalBloodrageIncrement = 7;
	EXPECT_THROW(graph.requireBoundUnit(first, one), std::logic_error);
	EXPECT_THROW(graph.loadBoundState(first, one, JsonNode()), std::logic_error);
	EXPECT_EQ(one.personalBloodrageIncrement, 7);
}

TEST_F(ProjectedUnitInitializationTest, DistinctEqualIdNodesHaveIsolatedProjectedBonusesAndCountOwners)
{
	ProjectedBonusRoster roster({&one, &two}, ProjectedBonusRoster::Identity::OrderedOccurrences);
	initialize(roster);
	graph.addExported(first, initializationBonus(BonusType::STACKS_SPEED, 2));
	graph.addExported(second, initializationBonus(BonusType::STACKS_SPEED, 9));
	battle::StackInitializationReceipts firstReceipts(one), secondReceipts(two);
	firstReceipts.initialize(one, &environment, BattleHex(5, 5));
	secondReceipts.initialize(two, &environment, BattleHex(9, 5));
	EXPECT_EQ(one.valOfBonuses(BonusType::STACKS_SPEED), 2);
	EXPECT_EQ(two.valOfBonuses(BonusType::STACKS_SPEED), 9);
	EXPECT_EQ(one.getCount(), 8);
	EXPECT_EQ(two.getCount(), 3);
	EXPECT_EQ(BonusEvaluationContext(first, one, roster).stackCount(), 8);
	EXPECT_EQ(BonusEvaluationContext(second, two, roster).stackCount(), 3);
	EXPECT_EQ(roster.firstById(17), &one);
}

TEST_F(ProjectedUnitInitializationTest, AppendKeepsRosterAddressAndFirstLookupWhileBindingNewOccurrence)
{
	ProjectedBonusRoster roster({&one}, ProjectedBonusRoster::Identity::OrderedOccurrences);
	graph.bindOccurrence(first, one, roster);
	const auto * address = &roster;
	graph.appendOccurrence(second, two, roster);
	EXPECT_EQ(&roster, address);
	ASSERT_EQ(roster.orderedUnits().size(), 2);
	EXPECT_EQ(roster.orderedUnits()[1], &two);
	EXPECT_EQ(roster.firstById(17), &one);
	EXPECT_NO_THROW(graph.requireBoundUnit(second, two));
}

TEST_F(ProjectedUnitInitializationTest, RepeatedAppendPoisonsPreparationWithoutNativeMutation)
{
	ProjectedBonusRoster roster({&one}, ProjectedBonusRoster::Identity::OrderedOccurrences);
	graph.bindOccurrence(first, one, roster);
	const auto version = first.getTreeVersion();
	EXPECT_THROW(graph.appendOccurrence(first, one, roster), std::invalid_argument);
	EXPECT_EQ(first.getTreeVersion(), version);
	EXPECT_EQ(roster.orderedUnits().size(), 1);
	EXPECT_THROW(graph.requireBoundUnit(first, one), std::logic_error);
}

TEST_F(ProjectedUnitInitializationTest, WrongProviderAfterPrivateAppendRequiresWholeWorldAbandonment)
{
	ProjectedBonusRoster roster({&one}, ProjectedBonusRoster::Identity::OrderedOccurrences);
	graph.bindOccurrence(first, one, roster);
	battle::CUnitStateDetached ordinary(&second, &second);
	EXPECT_THROW(graph.appendOccurrence(second, ordinary, roster), std::invalid_argument);
	// Private prefix is NOT rolled back or published. No query may use it.
	EXPECT_EQ(roster.orderedUnits().size(), 2);
	EXPECT_THROW(graph.requireBoundUnit(first, one), std::logic_error);
	EXPECT_TRUE(liveBonusGraph().parents(second).empty());
}

TEST_F(ProjectedUnitInitializationTest, SharedInitializerRestoresEveryLegacyReceiptAfterReset)
{
	ProjectedBonusRoster roster({&one, &two}, ProjectedBonusRoster::Identity::OrderedOccurrences);
	initialize(roster);
	seedReceipts();
	const auto confusion = one.confusionState;
	battle::StackInitializationReceipts receipts(one);
	receipts.initialize(one, &environment, BattleHex(5, 5));
	EXPECT_EQ(one.personalBloodrageIncrement, 7);
	EXPECT_TRUE(one.waiting && one.waitedThisTurn && one.movedThisRound);
	EXPECT_FALSE(one.defending);
	EXPECT_TRUE(one.hadMorale && one.moraleExtraActivation);
	EXPECT_TRUE(one.battlecraftWaitBonusUsed && one.battlecraftWaitMasteryDoubled);
	EXPECT_TRUE(one.veteranCohesionEarned);
	EXPECT_EQ(one.confusionState, confusion);
	EXPECT_EQ(one.battlecraftOverwatchReadyRound, 2);
	EXPECT_EQ(one.battlecraftOverwatchUsedRound, 3);
	EXPECT_TRUE(one.heroicSpiritRetaliation && one.heroicSpiritMoralePending);
	EXPECT_EQ(one.pursuitMovementRemaining, 4);
	EXPECT_TRUE(one.luckyOwnAttackSequence);
	EXPECT_EQ(one.frozenLastAppliedRound(), 5);
	EXPECT_TRUE(one.timeStopTurnConsumedFlag);
	EXPECT_EQ(one.veteranPhysicalDamageSinceActivation, 11);
	EXPECT_EQ(one.noQuarterMoraleActivationsRemaining, 2);
	EXPECT_EQ(one.getPosition(), BattleHex(5, 5));
	EXPECT_EQ(one.getCount(), 8);
	EXPECT_EQ(one.getAvailableHealth(), 160);
}

TEST_F(ProjectedUnitInitializationTest, ResetStillClearsNoncarriedValuesAndSummonFlagsBeforeHealth)
{
	ProjectedBonusRoster roster({&one, &two}, ProjectedBonusRoster::Identity::OrderedOccurrences);
	initialize(roster);
	one.summoned = one.natureSummoned = true;
	auto summonHealth = initializationBonus(BonusType::STACK_HEALTH, 1);
	summonHealth->updater = std::make_shared<SummonSensitiveInitializationUpdater>();
	graph.addExported(first, summonHealth);
	one.ghost = one.cloned = true;
	one.cloneID = 29;
	one.guardianSpiritHitPoints = 40;
	one.setActivationMovementBonus(3);
	battle::StackInitializationReceipts receipts(one);
	receipts.initialize(one, &environment, BattleHex(5, 5));
	EXPECT_FALSE(one.summoned || one.natureSummoned || one.ghost || one.cloned);
	EXPECT_EQ(one.cloneID, -1);
	EXPECT_EQ(one.guardianSpiritHitPoints, 0);
	EXPECT_EQ(one.getActivationMovementBonus(), 0);
	EXPECT_EQ(one.getAvailableHealth(), 160);
	// The ADD owner, NOT this shared reset owner, restores packet provenance.
	one.summoned = one.natureSummoned = true;
	EXPECT_TRUE(one.summoned && one.natureSummoned);
}

TEST_F(ProjectedUnitInitializationTest, DefendingAndUnarmedQueueReceiptsAreNotReinterpreted)
{
	ProjectedBonusRoster roster({&one, &two}, ProjectedBonusRoster::Identity::OrderedOccurrences);
	initialize(roster);
	one.defending = true;
	battle::StackInitializationReceipts receipts(one);
	receipts.initialize(one, &environment, BattleHex(5, 5));
	EXPECT_TRUE(one.defending);
	EXPECT_FALSE(one.waiting || one.waitedThisTurn || one.battlecraftWaitBonusUsed);
	EXPECT_FALSE(one.battlecraftWaitMasteryDoubled || one.hadMorale);
}

TEST_F(ProjectedUnitInitializationTest, ResetQueryFailurePropagatesAndCallerAbandonsPrivateWorld)
{
	ProjectedBonusRoster roster({&one, &two}, ProjectedBonusRoster::Identity::OrderedOccurrences);
	initialize(roster);
	auto rejected = initializationBonus(BonusType::STACK_HEALTH, 1);
	rejected->updater = std::make_shared<RejectingInitializationUpdater>();
	graph.addExported(first, rejected);
	one.personalBloodrageIncrement = 7;
	battle::StackInitializationReceipts receipts(one);
	const auto version = first.getTreeVersion();
	EXPECT_THROW(receipts.initialize(one, &environment, BattleHex(5, 5)), std::runtime_error);
	graph.abandonPreparation();
	EXPECT_EQ(first.getTreeVersion(), version);
	EXPECT_EQ(first.personalBloodrageIncrement, 0);
	EXPECT_THROW(graph.requireBoundUnit(first, one), std::logic_error);
}

TEST_F(ProjectedUnitInitializationTest, BoundBearerExposesExactSourceLocalSpellsWithoutInheritance)
{
	ProjectedBonusRoster roster({&one, &two}, ProjectedBonusRoster::Identity::OrderedOccurrences);
	initialize(roster);
	auto local = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::MAGIC_RESISTANCE,
		BonusSource::SPELL_EFFECT, 3, BonusSourceID(SpellID(SpellID::HASTE)));
	local->turnsRemain = 3;
	auto inherited = std::make_shared<Bonus>(*local);
	inherited->val = 7;
	graph.addExported(first, local);
	graph.addExported(second, inherited);
	graph.attachToSource(first, second);
	const auto * view = dynamic_cast<const ProjectedBonusView *>(one.getBonusBearer());
	ASSERT_NE(view, nullptr);
	EXPECT_EQ(view, &graph.view(first));
	EXPECT_EQ(view->evaluatedUnit(), &one);
	EXPECT_EQ(one.getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::HASTE))))->size(), 2);
	const auto source = view->sourceLocalSpellEffects();
	ASSERT_EQ(source.size(), 1);
	EXPECT_EQ(source.front().val, 3);
	EXPECT_EQ(source.front().turnsRemain, 3);
	EXPECT_TRUE(first.getExportedBonusList().empty());
	EXPECT_TRUE(second.getExportedBonusList().empty());
}

TEST_F(ProjectedUnitInitializationTest, OrdinaryBearerStaysLocalAndForeignProjectedBearerRefuses)
{
	ProjectedBonusRoster roster({&one}, ProjectedBonusRoster::Identity::OrderedOccurrences);
	graph.bindOccurrence(first, one, roster);
	battle::CUnitStateDetached ordinary(&first, &first);
	EXPECT_EQ(ordinary.getBonusBearer(), &ordinary);
	battle::CUnitStateDetached wrong(&second, &graph.view(first));
	EXPECT_THROW(wrong.getBonusBearer(), std::logic_error);
}

TEST_F(ProjectedUnitInitializationTest, BoundProviderRoundTripsEarnedMoraleAndRejectsUnearnedMarker)
{
	ProjectedBonusRoster roster({&one, &two}, ProjectedBonusRoster::Identity::OrderedOccurrences);
	initialize(roster);
	battle::StackInitializationReceipts receipts(one);
	receipts.initialize(one, &environment, BattleHex(5, 5));
	one.hadMorale = one.moraleExtraActivation = true;
	const auto saved = one.save();
	ASSERT_TRUE(saved["state"]["hadMorale"].Bool());
	ASSERT_TRUE(saved["state"]["moraleExtraActivation"].Bool());
	one.hadMorale = one.moraleExtraActivation = false;
	ASSERT_NO_THROW(one.load(saved));
	EXPECT_TRUE(one.hadMorale && one.moraleExtraActivation);
	EXPECT_EQ(one.save(), saved);
	EXPECT_EQ(one.getBonusBearer(), &graph.view(first));
	EXPECT_EQ(graph.view(first).evaluatedUnit(), &one);
	auto corrupt = saved;
	corrupt["state"]["hadMorale"].Bool() = false;
	// A fresh bound probe tests the existing validator, not rollback semantics.
	try
	{
		two.load(corrupt);
		FAIL() << "Unearned Morale activation marker was accepted";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_STREQ(error.what(), "Morale activation provenance requires its earned receipt");
	}
}
}
