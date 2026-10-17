/*
 * PreparedBattleUnitRootsTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../../lib/battle/PreparedBattleUnitRoots.h"
#include "../../lib/battle/ProjectedBattleWorldArena.h"
#include "../../lib/bonuses/ProjectedBonusRoster.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include <stdexcept>
#include <type_traits>

namespace
{
using Plan = battle::PreparedBattleUnitRoots;
using Arena = battle::ProjectedBattleWorldArena;
static_assert(!std::is_copy_constructible_v<Plan> && !std::is_move_constructible_v<Plan>);
static_assert(noexcept(std::declval<Plan &>().commit()));
static_assert(noexcept(std::declval<const Plan &>().readyToCommit()));
static_assert(noexcept(std::declval<CStack &>().bindStateEnvironment(nullptr)));
static_assert(noexcept(std::declval<battle::CShots &>().setEnv(nullptr)));
static_assert(noexcept(std::declval<battle::CRetaliations &>().setEnv(nullptr)));
static_assert(noexcept(std::declval<std::unique_ptr<CStack> &>().swap(std::declval<std::unique_ptr<CStack> &>())));
static_assert(noexcept(std::declval<std::vector<std::unique_ptr<CStack>> &>().swap(
	std::declval<std::vector<std::unique_ptr<CStack>> &>())));

class PreparedBattleUnitRootsTest : public BattleTestFixture
{
protected:
	std::unique_ptr<Arena> arena;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame();
		startBattle();
	}
	void TearDown() override
	{
		arena.reset(); // private graph/finalizer BEFORE native battlefield roots
		BattleTestFixture::TearDown();
	}
	void capture() { arena = std::make_unique<Arena>(*battle()); }
	JsonNode addition()
	{
		battle::UnitInfo info;
		info.type = CreatureID(0);
		info.count = 4;
		info.side = BattleSide::ATTACKER;
		info.position = BattleHex(leftHex);
		info.summoned = true;
		JsonNode data;
		info.save(data);
		return data;
	}
	std::vector<const CStack *> roots() const
	{
		std::vector<const CStack *> result;
		for(const auto & stack : battle()->stacks)
			result.push_back(stack.get());
		return result;
	}
};

TEST_F(PreparedBattleUnitRootsTest, MultipleAddsPreservePhysicalAddressesOrderAndRawValues)
{
	const auto original = roots();
	capture();
	const auto firstId = arena->nextUnitId();
	arena->addUnit(firstId, addition());
	const auto * first = &arena->originalAt(original.size());
	const auto secondId = arena->nextUnitId();
	arena->addUnit(secondId, addition());
	const auto * second = &arena->originalAt(original.size() + 1);
	EXPECT_EQ(first->getBattle(), nullptr);
	EXPECT_EQ(second->getBattle(), nullptr);
	Plan plan(*battle(), *arena);
	EXPECT_EQ(roots(), original);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	ASSERT_TRUE(plan.commit());
	ASSERT_EQ(battle()->stacks.size(), original.size() + 2);
	for(size_t i = 0; i < original.size(); ++i)
		EXPECT_EQ(battle()->stacks[i].get(), original[i]);
	EXPECT_EQ(battle()->stacks[original.size()].get(), first);
	EXPECT_EQ(battle()->stacks[original.size() + 1].get(), second);
	EXPECT_EQ(first->unitId(), firstId);
	EXPECT_EQ(second->unitId(), secondId);
	EXPECT_EQ(first->getCount(), 4);
	EXPECT_EQ(second->getCount(), 4);
	EXPECT_EQ(first->getBattle(), battle());
	EXPECT_EQ(second->getBattle(), battle());
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_THROW(arena->size(), std::logic_error);
	// No native bonus/save query: graph/common world installation is still absent.
}

TEST_F(PreparedBattleUnitRootsTest, AddedThenRemovedOccurrenceRemainsOwnedAndGhosted)
{
	capture();
	const auto index = arena->size();
	const auto id = arena->nextUnitId();
	arena->addUnit(id, addition());
	const auto * original = &arena->originalAt(index);
	ASSERT_TRUE(arena->removeValuesAndEdges(id).completed);
	ASSERT_TRUE(arena->unitAt(index).isGhost());
	Plan plan(*battle(), *arena);
	ASSERT_TRUE(plan.commit());
	ASSERT_EQ(battle()->stacks.size(), index + 1);
	EXPECT_EQ(battle()->stacks.back().get(), original);
	EXPECT_TRUE(original->isGhost());
}

TEST_F(PreparedBattleUnitRootsTest, NoAddsPreserveRootAllocationCapacityAndIdentity)
{
	capture();
	const auto original = roots();
	const auto * data = battle()->stacks.data();
	const auto capacity = battle()->stacks.capacity();
	Plan plan(*battle(), *arena);
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(roots(), original);
	EXPECT_EQ(battle()->stacks.data(), data);
	EXPECT_EQ(battle()->stacks.capacity(), capacity);
}

TEST_F(PreparedBattleUnitRootsTest, ExistingRemoveDoesNotEraseOrMoveNativeOccurrences)
{
	const auto original = roots();
	ASSERT_FALSE(original.empty());
	capture();
	ASSERT_TRUE(arena->removeValuesAndEdges(original.front()->unitId()).completed);
	ASSERT_TRUE(arena->unitAt(0).isGhost());
	const auto originalGhost = original.front()->isGhost();
	Plan plan(*battle(), *arena);
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(roots(), original);
	// Existing values are deliberately a DIFFERENT common-commit participant.
	EXPECT_EQ(original.front()->isGhost(), originalGhost);
}

TEST_F(PreparedBattleUnitRootsTest, BindingOriginCannotBeRecapturedAtPlanConstruction)
{
	capture();
	battle()->stacks.reserve(battle()->stacks.capacity() + 1);
	const auto original = roots();
	const auto * data = battle()->stacks.data();
	EXPECT_THROW(Plan(*battle(), *arena), std::runtime_error);
	EXPECT_EQ(roots(), original);
	EXPECT_EQ(battle()->stacks.data(), data);
}

TEST_F(PreparedBattleUnitRootsTest, WrongBattleRefusesWithoutEitherRootWrite)
{
	capture();
	BattleInfo other(nullptr);
	const auto original = roots();
	EXPECT_THROW(Plan(other, *arena), std::invalid_argument);
	EXPECT_TRUE(other.stacks.empty());
	EXPECT_EQ(roots(), original);
}

TEST_F(PreparedBattleUnitRootsTest, ReallocatedRootAfterPreparationRefusesBeforeTransfer)
{
	capture();
	arena->addUnit(arena->nextUnitId(), addition());
	Plan plan(*battle(), *arena);
	battle()->stacks.reserve(battle()->stacks.capacity() + 1);
	const auto original = roots();
	EXPECT_FALSE(plan.readyToCommit());
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(roots(), original);
	EXPECT_EQ(arena->size(), original.size() + 1);
}

TEST_F(PreparedBattleUnitRootsTest, ChangedPhysicalOrderRefusesEvenWhenCountAndAllocationMatch)
{
	capture();
	Plan plan(*battle(), *arena);
	ASSERT_GE(battle()->stacks.size(), 2);
	battle()->stacks[0].swap(battle()->stacks[1]);
	const auto reordered = roots();
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(roots(), reordered);
}

TEST_F(PreparedBattleUnitRootsTest, ChangedOriginalDescriptorRefusesWithoutAppendingPrefix)
{
	capture();
	arena->addUnit(arena->nextUnitId(), addition());
	Plan plan(*battle(), *arena);
	ASSERT_FALSE(battle()->stacks.empty());
	battle()->stacks.front()->initialPosition = BattleHex(rightHex);
	const auto original = roots();
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(roots(), original);
	EXPECT_EQ(battle()->stacks.front()->initialPosition, BattleHex(rightHex));
}

TEST_F(PreparedBattleUnitRootsTest, ColdCreationIdentityIsNotRecapturedAfterPrivateMutation)
{
	capture();
	const auto index = arena->size();
	arena->addUnit(arena->nextUnitId(), addition());
	auto & cold = const_cast<CStack &>(arena->originalAt(index));
	cold.initialPosition = BattleHex(rightHex);
	const auto original = roots();
	EXPECT_THROW(Plan(*battle(), *arena), std::runtime_error);
	EXPECT_EQ(roots(), original);
	EXPECT_EQ(cold.getBattle(), nullptr);
}

TEST_F(PreparedBattleUnitRootsTest, LaterPrivateAppendInvalidatesEarlierPlan)
{
	capture();
	arena->addUnit(arena->nextUnitId(), addition());
	Plan plan(*battle(), *arena);
	arena->addUnit(arena->nextUnitId(), addition());
	const auto original = roots();
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(roots(), original);
}

TEST_F(PreparedBattleUnitRootsTest, FailedLaterPrivateEntryCannotPublishEarlierAddition)
{
	capture();
	arena->addUnit(arena->nextUnitId(), addition());
	Plan plan(*battle(), *arena);
	auto invalid = addition();
	invalid.Struct().erase("type");
	const auto original = roots();
	EXPECT_THROW(arena->addUnit(arena->nextUnitId(), invalid), std::exception);
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(roots(), original);
}

TEST_F(PreparedBattleUnitRootsTest, AdvancedGlobalGenerationRefusesBeforeAnyRootOrBindingWrite)
{
	capture();
	const auto index = arena->size();
	arena->addUnit(arena->nextUnitId(), addition());
	const auto * cold = &arena->originalAt(index);
	Plan plan(*battle(), *arena);
	battle()->nodeHasChanged();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	const auto original = roots();
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(roots(), original);
	EXPECT_EQ(cold->getBattle(), nullptr);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(PreparedBattleUnitRootsTest, CompetingPlanCannotRestoreConsumedOwnership)
{
	capture();
	arena->addUnit(arena->nextUnitId(), addition());
	Plan first(*battle(), *arena), second(*battle(), *arena);
	ASSERT_TRUE(first.commit());
	const auto committed = roots();
	EXPECT_FALSE(first.commit());
	EXPECT_FALSE(second.readyToCommit());
	EXPECT_FALSE(second.commit());
	EXPECT_EQ(roots(), committed);
}

TEST_F(PreparedBattleUnitRootsTest, OldAllocationAndNativeIdentitiesSurviveControlledPlanRetirement)
{
	capture();
	const auto original = roots();
	const auto * retiredAllocation = battle()->stacks.data();
	arena->addUnit(arena->nextUnitId(), addition());
	auto plan = std::make_unique<Plan>(*battle(), *arena);
	ASSERT_TRUE(plan->commit());
	for(size_t i = 0; i < original.size(); ++i)
		EXPECT_EQ(retiredAllocation[i].get(), nullptr); // allocation held by plan
	const auto committed = roots();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	plan.reset(); // releases null old vector, never native CStack objects
	EXPECT_EQ(roots(), committed);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	arena.reset(); // private graph/identities retire while all native roots live
	EXPECT_EQ(roots(), committed);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(PreparedBattleUnitRootsTest, RebirthQueryOnlyIdentityNeverBecomesBattlefieldRoot)
{
	capture();
	const auto index = arena->size();
	auto data = addition();
	data["rebirthOriginalAggregateHP"].Integer() = 1;
	arena->addUnit(arena->nextUnitId(), data);
	ASSERT_EQ(arena->retainedQueryCount(), 1);
	ASSERT_EQ(arena->roster().orderedUnits().size(), index + 1);
	Plan plan(*battle(), *arena);
	ASSERT_TRUE(plan.commit());
	ASSERT_EQ(battle()->stacks.size(), index + 1);
	for(const auto & unit : battle()->stacks)
		EXPECT_NE(unit->unitId(), uint32_t(-1));
}

TEST_F(PreparedBattleUnitRootsTest, AddedFormTransfersViewAndFootprintWithoutNativeHookReplay)
{
	capture();
	const auto index = arena->size();
	arena->addUnit(arena->nextUnitId(), addition());
	auto & state = const_cast<battle::CUnitStateDetached &>(arena->unitAt(index));
	state.beginBattleForm(CreatureID(2), 3); // exact projected notification provider
	ASSERT_TRUE(state.hasBattleForm());
	const auto revision = state.getBattleFormViewRevision();
	const auto * cold = &arena->originalAt(index);
	ASSERT_FALSE(cold->hasBattleForm());
	Plan plan(*battle(), *arena);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	ASSERT_TRUE(plan.commit());
	EXPECT_TRUE(cold->hasBattleForm());
	EXPECT_EQ(cold->battleFormCreature(), CreatureID(2));
	EXPECT_EQ(cold->doubleWide(), CreatureID(2).toCreature()->isDoubleWide());
	EXPECT_EQ(cold->getBattleFormViewRevision(), revision);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(PreparedBattleUnitRootsTest, RootParticipantPrecedesGraphFreezeAndRemainsPreCasReady)
{
	capture();
	arena->addUnit(arena->nextUnitId(), addition());
	Plan plan(*battle(), *arena);
	TNodesVector authority;
	for(const auto * node : arena->capturedGraphNodes())
		authority.push_back(const_cast<CBonusSystemNode *>(node)); // fixture-only exclusive source ownership
	const auto original = roots();
	arena->prepareGraphStorage(authority);
	ASSERT_TRUE(arena->graphStorageReady());
	EXPECT_TRUE(plan.readyToCommit());
	EXPECT_EQ(roots(), original);
	EXPECT_THROW(Plan(*battle(), *arena), std::logic_error);
}

class LegacyPreparedBattleUnitRootsTest : public PreparedBattleUnitRootsTest
{
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		JsonNode legacyRules;
		legacyRules.Struct();
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, legacyRules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode());
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode());
	}
};

TEST_F(LegacyPreparedBattleUnitRootsTest, DuplicatePacketIdsKeepOrderedPhysicalOccurrencesAndFirstMatch)
{
	capture();
	ASSERT_GT(arena->size(), 0);
	const auto first = &arena->originalAt(0);
	const auto id = first->unitId();
	const auto index = arena->size();
	arena->addUnit(id, addition());
	const auto second = &arena->originalAt(index);
	ASSERT_NE(first, second);
	ASSERT_EQ(arena->findUnit(id), &arena->unitAt(0));
	Plan plan(*battle(), *arena);
	ASSERT_TRUE(plan.commit());
	ASSERT_EQ(battle()->stacks.size(), index + 1);
	EXPECT_EQ(battle()->stacks[0].get(), first);
	EXPECT_EQ(battle()->stacks[index].get(), second);
	EXPECT_EQ(first->unitId(), second->unitId());
	EXPECT_EQ(battle()->getStack(id, false), first);
}
}
