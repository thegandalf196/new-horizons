/*
 * ProjectedBonusGraphTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../mock/mock_BonusBearer.h"
#include "../mock/mock_UnitEnvironment.h"
#include "../mock/mock_UnitInfo.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/ProjectedBonusGraph.h"
#include "../../lib/bonuses/ProjectedBonusRoster.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/bonuses/BonusCache.h"
#include "../../lib/bonuses/BonusEvaluationContext.h"
#include "../../lib/bonuses/PreparedBonusGraphFinalization.h"
#include "../../lib/CStack.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/UnitStateValueFields.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <stdexcept>

namespace test
{
using namespace ::testing;
namespace
{
std::shared_ptr<Bonus> value(BonusType type, int amount)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, type, BonusSource::OTHER, amount, BonusSourceID());
}

std::vector<std::byte> bytes(const Bonus & bonus)
{
	CMemorySerializer memory;
	memory.oser & bonus;
	return memory.extractBuffer();
}

class RatingNode final : public CBonusSystemNode
{
public:
	RatingNode() : CBonusSystemNode(BonusNodeType::HERO) {}
	bool isPrimaryRatingNode() const override { return true; }
};

class ThrowingUpdater final : public IUpdater
{
public:
	std::shared_ptr<Bonus> createUpdatedBonus(const std::shared_ptr<Bonus> &, const BonusEvaluationContext &) const override
	{
		throw std::runtime_error("synthetic projected updater rejection");
	}
};

class IdentityRecordingUpdater final : public IUpdater
{
	const CBonusSystemNode & expectedSource;
public:
	explicit IdentityRecordingUpdater(const CBonusSystemNode & source) : expectedSource(source) {}
	std::shared_ptr<Bonus> createUpdatedBonus(const std::shared_ptr<Bonus> & bonus, const BonusEvaluationContext & context) const override
	{
		auto result = std::make_shared<Bonus>(*bonus);
		result->val += &context.originalNode() == &expectedSource ? 100 : -100;
		return result;
	}
};

class ScopedCreatureExports final : boost::noncopyable
{
	CStack & stack;
	CCreature & source;
	CBonusSystemNode & first;
	CBonusSystemNode & second;
	std::shared_ptr<Bonus> local;
public:
	ScopedCreatureExports(CStack & stack, CCreature & source, CBonusSystemNode & first,
		CBonusSystemNode & second, const std::shared_ptr<Bonus> & local)
		: stack(stack), source(source), first(first), second(second), local(local)
	{
		source.attachToSource(first);
		source.attachToSource(second);
		source.addNewBonus(local);
	}
	~ScopedCreatureExports()
	{
		if(stack.hasBattleForm())
			stack.endBattleForm();
		source.removeBonus(local);
		source.detachFromSource(second);
		source.detachFromSource(first);
	}
};
}

class ProjectedBonusGraphTest : public Test
{
protected:
	CStackBasicDescriptor descriptor{CreatureID(0), 8};
	CStack original{&descriptor, PlayerColor(0), 17, BattleSide::ATTACKER};
	CStackInstance stableBase{nullptr, CreatureID(0), 8, true};
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
		auto health = value(BonusType::STACK_HEALTH, 20);
		health->source = BonusSource::CREATURE_ABILITY;
		health->sid = BonusSourceID(CreatureID(0));
		original.addNewBonus(health);
		// All terrain queries in this suite have a defined adventure-base value;
		// do not execute the separate pre-existing baseless-terrain defect.
		original.base = &stableBase;
		original.health.init();
		original.position = BattleHex(5, 5);
	}

	void initialize()
	{
		unit = std::make_unique<battle::CUnitStateDetached>(&info, &graph.view(original));
		battle::UnitStateValueFields::copy(*unit, original); // unbound snapshot, no hook
		unit->bindStateEnvironment(&environment);
		roster = std::make_unique<ProjectedBonusRoster>(std::vector<const battle::Unit *>{unit.get()});
		graph.bind(original, *unit, *roster);
	}

	void parity(const IBonusBearer & first, const IBonusBearer & second)
	{
		for(bool unstacked : {false, true})
		{
			const auto a = unstacked ? first.getUnstackedBonuses(Selector::all) : first.getAllBonuses(Selector::all);
			const auto b = unstacked ? second.getUnstackedBonuses(Selector::all) : second.getAllBonuses(Selector::all);
			ASSERT_EQ(a->size(), b->size());
			for(size_t index = 0; index < a->size(); ++index)
				EXPECT_EQ(bytes(*(*a)[index]), bytes(*(*b)[index]));
		}
	}
};

TEST_F(ProjectedBonusGraphTest, ActualRecursiveLiveParityRetainsDiamondMultiplicity)
{
	CBonusSystemNode top(BonusNodeType::PLAYER), first(BonusNodeType::ARMY), second(BonusNodeType::ARMY);
	auto speed = value(BonusType::STACKS_SPEED, 4);
	top.addNewBonus(speed);
	first.attachTo(top);
	second.attachTo(top);
	original.attachTo(first);
	original.attachTo(second);
	initialize();
	parity(original, *unit);
	const auto unstacked = unit->getUnstackedBonuses(Selector::type()(BonusType::STACKS_SPEED));
	ASSERT_EQ(unstacked->size(), 2);
	EXPECT_EQ((*unstacked)[0], speed);
	EXPECT_EQ((*unstacked)[1], speed);
	EXPECT_EQ(unit->getAllBonuses(Selector::type()(BonusType::STACKS_SPEED))->size(), 1);
}

TEST_F(ProjectedBonusGraphTest, ChangedAncestorFeedsNestedHealthAndBypassesWarmUnitCache)
{
	CBonusSystemNode ancestor(BonusNodeType::PLAYER);
	auto extra = value(BonusType::STACK_HEALTH, 5);
	ancestor.addNewBonus(extra);
	original.attachTo(ancestor);
	initialize();
	const auto version = original.getTreeVersion();
	EXPECT_EQ(unit->getMaxHealth(), 25);
	EXPECT_EQ(original.getMaxHealth(), 25);
	auto changed = graph.values(ancestor);
	auto replacement = std::make_shared<Bonus>(*extra);
	replacement->val = 12;
	changed.accepted.clear(); changed.exported.clear();
	changed.accepted.push_back(replacement); changed.exported.push_back(replacement);
	graph.replace(ancestor, std::move(changed));
	EXPECT_EQ(unit->getMaxHealth(), 32);
	EXPECT_EQ(unit->getMaxHealth(), 32);
	EXPECT_EQ(original.getMaxHealth(), 25);
	EXPECT_EQ(original.getTreeVersion(), version);
	EXPECT_EQ(extra->val, 5);
	EXPECT_FALSE(unit->isBonusCacheable());
	EXPECT_TRUE(original.isBonusCacheable());
}

TEST_F(ProjectedBonusGraphTest, ChangedCountReevaluatesInheritedUpdaterAndExplicitValueCache)
{
	CBonusSystemNode ancestor(BonusNodeType::PLAYER);
	auto speed = value(BonusType::STACKS_SPEED, 99);
	speed->updater = std::make_shared<TimesStackSizeUpdater>();
	ancestor.addNewBonus(speed);
	original.attachTo(ancestor);
	initialize();
	BonusValueCache cache(unit.get(), Selector::type()(BonusType::STACKS_SPEED));
	EXPECT_EQ(cache.getValue(), 8);
	int64_t damage = 20;
	unit->damage(damage);
	ASSERT_EQ(unit->getCount(), 7);
	EXPECT_EQ(cache.getValue(), 7);
	EXPECT_EQ(original.getCount(), 8);
	EXPECT_EQ(original.valOfBonuses(BonusType::STACKS_SPEED), 8);
	EXPECT_EQ(speed->val, 99);
}

TEST_F(ProjectedBonusGraphTest, PrimaryAncestorFilteringKeepsOnlyCreatureSpecificRating)
{
	RatingNode hero;
	auto ordinary = value(BonusType::PRIMARY_SKILL, 4);
	ordinary->subtype = BonusSubtypeID(PrimarySkill::ATTACK);
	auto specific = std::make_shared<Bonus>(*ordinary);
	specific->val = 13;
	specific->limiter = std::make_shared<CCreatureTypeLimiter>(*CreatureID(0).toCreature(), false);
	hero.addNewBonus(ordinary); hero.addNewBonus(specific);
	original.attachTo(hero);
	initialize();
	parity(original, *unit);
	EXPECT_EQ(unit->valOfBonuses(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::ATTACK)), 13);
}

TEST_F(ProjectedBonusGraphTest, PerTurnCacheAlsoBypassesRetainedListsAndWarmTurnSlots)
{
	CBonusSystemNode ancestor(BonusNodeType::PLAYER);
	auto speed = value(BonusType::STACKS_SPEED, 4);
	ancestor.addNewBonus(speed);
	original.attachTo(ancestor);
	initialize();
	BonusCachePerTurn cache(unit.get(), Selector::type()(BonusType::STACKS_SPEED), BonusCacheMode::VALUE);
	EXPECT_EQ(cache.getValue(0), 4);
	EXPECT_EQ(cache.getValue(1), 4);
	graph.removeExported(ancestor, speed);
	graph.addExported(ancestor, value(BonusType::STACKS_SPEED, 9));
	EXPECT_EQ(cache.getValue(0), 9);
	EXPECT_EQ(cache.getValue(1), 9);
	EXPECT_EQ(cache.getValue(10), 9);
	EXPECT_EQ(original.valOfBonuses(BonusType::STACKS_SPEED), 4);
}

TEST_F(ProjectedBonusGraphTest, OwnerAndAncestorLimiterUseSameProjectedContext)
{
	EXPECT_CALL(info, unitOwner()).WillRepeatedly(Return(PlayerColor(2)));
	CBonusSystemNode ancestor(BonusNodeType::PLAYER);
	auto speed = value(BonusType::STACKS_SPEED, 9);
	speed->bonusOwner = PlayerColor(0);
	speed->limiter = std::make_shared<OppositeSideLimiter>();
	ancestor.addNewBonus(speed);
	original.attachTo(ancestor);
	initialize();
	EXPECT_EQ(original.valOfBonuses(BonusType::STACKS_SPEED), 0);
	EXPECT_EQ(unit->valOfBonuses(BonusType::STACKS_SPEED), 9);
	EXPECT_CALL(info, unitOwner()).WillRepeatedly(Return(PlayerColor(0)));
	EXPECT_EQ(unit->valOfBonuses(BonusType::STACKS_SPEED), 0);
}

TEST_F(ProjectedBonusGraphTest, DetachedFormQueriesUseProjectedSourcesNotLegacyLiveEvaluator)
{
	initialize();
	const auto originalParents = original.getParentNodes();
	const auto originalVersion = original.getTreeVersion();
	unit->beginBattleForm(CreatureID(2), 3);
	EXPECT_EQ(unit->unitType(), CreatureID(2).toCreature());
	EXPECT_EQ(unit->getMaxHealth(), CreatureID(2).toCreature()->getBaseHitPoints());
	EXPECT_TRUE(vstd::contains(graph.parents(original), CreatureID(2).toCreature()));
	EXPECT_EQ(original.getParentNodes(), originalParents);
	EXPECT_EQ(original.getTreeVersion(), originalVersion);
	EXPECT_EQ(original.getMaxHealth(), 20);
	unit->endBattleForm();
	EXPECT_EQ(unit->getMaxHealth(), 20);
	EXPECT_FALSE(vstd::contains(graph.parents(original), CreatureID(2).toCreature()));
}

TEST_F(ProjectedBonusGraphTest, ExistingPropagationIsMaterializedOnceNotExportedAgain)
{
	CBonusSystemNode battle(BonusNodeType::BATTLE_WIDE);
	original.attachTo(battle);
	auto aura = value(BonusType::STACKS_SPEED, 6);
	aura->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::BATTLE_WIDE);
	original.addNewBonus(aura);
	initialize();
	parity(original, *unit);
	const auto effects = unit->getUnstackedBonuses(Selector::type()(BonusType::STACKS_SPEED));
	ASSERT_EQ(effects->size(), 1);
	EXPECT_EQ((*effects)[0], aura);
}

TEST_F(ProjectedBonusGraphTest, ActiveFormCopyBindsEnvironmentWithoutResetOrSourceReattachment)
{
	original.beginBattleForm(CreatureID(2), 3);
	original.defending = true;
	const auto snapshot = original.save();
	const auto originalParents = original.getParentNodes();
	const auto version = original.getTreeVersion();
	initialize();
	EXPECT_EQ(unit->save(), snapshot);
	EXPECT_TRUE(unit->defending);
	EXPECT_EQ(unit->getBattleFormRoundsRemaining(), 3);
	parity(original, *unit);
	graph.synchronizeForm(original);
	EXPECT_EQ(graph.parents(original), originalParents);
	EXPECT_EQ(original.getTreeVersion(), version);
	EXPECT_EQ(unit->save(), snapshot);
}

TEST_F(ProjectedBonusGraphTest, ProjectedExportPropagationUsesChangedSourceCountAndRemovalIdentity)
{
	CBonusSystemNode battle(BonusNodeType::BATTLE_WIDE);
	original.attachTo(battle);
	initialize();
	auto aura = value(BonusType::STACKS_SPEED, 99);
	aura->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::BATTLE_WIDE);
	aura->propagationUpdater = std::make_shared<TimesStackSizeUpdater>();
	graph.addExported(original, aura);
	EXPECT_EQ(unit->valOfBonuses(BonusType::STACKS_SPEED), 8);
	const auto & accepted = graph.values(battle).accepted;
	ASSERT_FALSE(accepted.empty());
	const auto propagated = accepted[accepted.size() - 1];
	EXPECT_NE(propagated, aura);
	EXPECT_EQ(propagated->propagationUpdater, aura->propagationUpdater);
	graph.removeExported(original, aura);
	EXPECT_EQ(unit->valOfBonuses(BonusType::STACKS_SPEED), 0);
	int64_t damage = 20; unit->damage(damage);
	ASSERT_EQ(unit->getCount(), 7);
	graph.addExported(original, aura);
	EXPECT_EQ(unit->valOfBonuses(BonusType::STACKS_SPEED), 7);
	EXPECT_EQ(original.valOfBonuses(BonusType::STACKS_SPEED), 0);
	EXPECT_EQ(aura->val, 99);
}

TEST_F(ProjectedBonusGraphTest, FormAncestorExportsUseAttachedSourceIdentityInLiveOrderAndRevert)
{
	// This fixture alone temporarily adds exports/edges to the installed source;
	// the scope guard restores them even after a fatal test assertion.
	auto & source = *const_cast<CCreature *>(CreatureID(2).toCreature());
	CBonusSystemNode first(BonusNodeType::ARTIFACT), second(BonusNodeType::ARTIFACT);
	const auto parents = source.getParentNodes();
	const auto exports = source.getExportedBonusList();
	const auto originalParents = original.getParentNodes();
	auto updater = std::make_shared<IdentityRecordingUpdater>(source);
	auto aura = [&](int amount)
	{
		auto bonus = value(BonusType::STACKS_SPEED, amount);
		bonus->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::STACK_BATTLE);
		bonus->propagationUpdater = updater;
		return bonus;
	};
	auto local = aura(1), firstExport = aura(2), secondExport = aura(3);
	first.addNewBonus(firstExport);
	second.addNewBonus(secondExport);
	auto identities = [&](const BonusList & bonuses)
	{
		std::vector<int> result;
		for(const auto & bonus : bonuses)
			if(bonus->propagationUpdater == updater)
				result.push_back(bonus->val);
		return result;
	};
	std::vector<int> expected{101};
	for(const auto * ancestor : TCNodes{&first, &second})
		expected.push_back(ancestor == &first ? 102 : 103);
	{
		ScopedCreatureExports restore(original, source, first, second, local);
		initialize();
		unit->beginBattleForm(CreatureID(2), 3);
		EXPECT_EQ(identities(graph.values(original).accepted), expected);
		unit->endBattleForm();
		EXPECT_TRUE(identities(graph.values(original).accepted).empty());
		original.beginBattleForm(CreatureID(2), 3);
		EXPECT_EQ(identities(*original.getUnstackedBonuses(Selector::all)), expected);
		original.endBattleForm();
		EXPECT_TRUE(identities(*original.getUnstackedBonuses(Selector::all)).empty());
		EXPECT_EQ(local->val, 1);
		EXPECT_EQ(firstExport->val, 2);
		EXPECT_EQ(secondExport->val, 3);
	}
	EXPECT_EQ(source.getParentNodes(), parents);
	ASSERT_EQ(source.getExportedBonusList().size(), exports.size());
	for(size_t index = 0; index < exports.size(); ++index)
		EXPECT_EQ(source.getExportedBonusList()[index], exports[index]);
	EXPECT_EQ(original.getParentNodes(), originalParents);
}

TEST_F(ProjectedBonusGraphTest, SameFinalFormRetainsEveryIntermediateGenerationEvent)
{
	initialize();
	const auto parents = original.getParentNodes();
	const auto exported = original.getExportedBonusList();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	for(int repeat = 0; repeat < 2; ++repeat)
	{
		unit->beginBattleForm(CreatureID(2), 3);
		unit->endBattleForm();
	}
	TNodesVector authority;
	// This single-threaded test library owns mutable fixture prototypes. The
	// production finalizer never derives write authority by casting const views.
	for(const auto * node : graph.capturedNodes())
		authority.push_back(const_cast<CBonusSystemNode *>(node));
	PreparedBonusGraphFinalization finalization(graph, authority);
	ASSERT_TRUE(finalization.commit());
	EXPECT_EQ(uint32_t(PreparedBonusGraphStorage::currentGeneration()), uint32_t(generation) + 8);
	EXPECT_EQ(original.getParentNodes(), parents);
	ASSERT_EQ(original.getExportedBonusList().size(), exported.size());
	for(size_t index = 0; index < exported.size(); ++index)
		EXPECT_EQ(original.getExportedBonusList()[index], exported[index]);
	EXPECT_FALSE(original.hasBattleForm()); // value state was never committed
}

TEST_F(ProjectedBonusGraphTest, QueryNamesDoNotAliasProjectedSelectorsOrOtherOwners)
{
	original.addNewBonus(value(BonusType::STACKS_SPEED, 6));
	initialize();
	const auto & view = graph.view(original);
	EXPECT_EQ(view.getAllBonuses(Selector::type()(BonusType::STACKS_SPEED), "same")->totalValue(), 6);
	EXPECT_EQ(view.getAllBonuses(Selector::type()(BonusType::STACK_HEALTH), "same")->totalValue(), 20);
	battle::CUnitStateDetached other(&info, &view);
	EXPECT_THROW(other.localInit(&environment), std::logic_error);
}

TEST_F(ProjectedBonusGraphTest, MissingEndpointsAndCyclesRefuseWithoutLiveWrites)
{
	CBonusSystemNode missing(BonusNodeType::PLAYER);
	initialize();
	const auto version = original.getTreeVersion();
	auto broken = graph.values(original);
	broken.parents.push_back(&missing);
	graph.replace(original, broken);
	EXPECT_THROW(unit->getMaxHealth(), std::logic_error);
	broken.parents.clear(); broken.parents.push_back(&original);
	graph.replace(original, broken);
	EXPECT_THROW(unit->getMaxHealth(), std::logic_error);
	EXPECT_EQ(original.getTreeVersion(), version);
	EXPECT_TRUE(original.getParentNodes().empty());
}

TEST_F(ProjectedBonusGraphTest, FailedPropagationPoisonsOnlyPrivateOwnerNoAcceptedPrefix)
{
	CBonusSystemNode battle(BonusNodeType::BATTLE_WIDE);
	original.attachTo(battle);
	initialize();
	const auto originalExports = original.getExportedBonusList();
	auto aura = value(BonusType::STACKS_SPEED, 9);
	aura->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::BATTLE_WIDE);
	aura->propagationUpdater = std::make_shared<ThrowingUpdater>();
	EXPECT_THROW(graph.addExported(original, aura), std::runtime_error);
	EXPECT_THROW(unit->getMaxHealth(), std::logic_error);
	EXPECT_EQ(original.getExportedBonusList().size(), originalExports.size());
	EXPECT_EQ(original.valOfBonuses(BonusType::STACKS_SPEED), 0);
}

TEST_F(ProjectedBonusGraphTest, ActualOriginalMutationInvalidatesBorrowedSnapshotBeforeQuery)
{
	initialize();
	original.addNewBonus(value(BonusType::STACKS_SPEED, 9));
	EXPECT_THROW(unit->getMaxHealth(), std::logic_error);
	EXPECT_EQ(original.valOfBonuses(BonusType::STACKS_SPEED), 9);
}
}
