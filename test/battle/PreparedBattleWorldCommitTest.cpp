/*
 * PreparedBattleWorldCommitTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../lib/battle/PreparedBattleWorldCommit.h"
#include "../../lib/battle/ProjectedBattleWorldArena.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/gameState/ReplayLog.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <limits>
#include <type_traits>

namespace
{
using Owner = battle::PreparedBattleWorldCommit;
static_assert(!std::is_copy_constructible_v<Owner> && !std::is_move_constructible_v<Owner>);
static_assert(noexcept(std::declval<Owner &>().commitSimulation()));
static_assert(noexcept(std::declval<const Owner &>().readyToCommit()));

class PreparedBattleWorldCommitTest : public HeroCommandFixture
{
protected:
	CStack * first = nullptr;
	CStack * second = nullptr;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		startGame();
		startBattle();
		first = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 4);
		second = addStack(BattleSide::DEFENDER, CreatureID(0), BattleHex(rightHex), 4);
		auto & log = gameHandler->gs->replayLog;
		log.configure(false, 2);
		log.beginDay(gameHandler->gs->day, {std::byte{0x11}}); // retained synthetic snapshot bytes, never replayed
	}
	BattleUnitsChanged defending()
	{
		BattleUnitsChanged packet;
		packet.battleID = battle()->getBattleID();
		UnitChanges change(first->unitId(), BattleChanges::EOperation::UPDATE);
		change.data = first->save();
		change.data["state"]["defending"].Bool() = true;
		packet.changedStacks.push_back(change);
		return packet;
	}
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
	std::unique_ptr<Owner> capture(const BattleUnitsChanged & packet)
	{
		return std::make_unique<Owner>(gameHandler->gs,
			std::unique_lock<std::shared_mutex>(CGameState::mutex), packet);
	}
	void prepare(Owner & owner)
	{
		TNodesVector authority;
		// Only this single-threaded fixture owns mutable source/library exclusion.
		// Production must supply real mutable owners, not infer them from this cast.
		for(const auto * node : owner.projected().capturedGraphNodes())
			authority.push_back(const_cast<CBonusSystemNode *>(node));
		owner.prepare(authority);
	}
	std::vector<std::byte> replayBytes()
	{
		CMemorySerializer serializer;
		serializer.oser & gameHandler->gs->replayLog;
		return serializer.extractBuffer();
	}
};

TEST_F(PreparedBattleWorldCommitTest, ValidUpdateTransfersWholeJsonAndExactReplayPackOnlyAfterPrepare)
{
	auto packet = defending();
	const auto expected = packet.changedStacks[0].data;
	const auto encoded = ReplayPackSerializer::write(packet);
	const auto before = replayBytes();
	auto owner = capture(packet);
	EXPECT_FALSE(first->defending);
	EXPECT_EQ(replayBytes(), before);
	EXPECT_FALSE(owner->readyToCommit());
	prepare(*owner);
	EXPECT_FALSE(first->defending);
	ASSERT_TRUE(owner->readyToCommit());
	ASSERT_TRUE(owner->commitSimulation());
	EXPECT_EQ(first->save(), expected);
	ASSERT_FALSE(gameHandler->gs->replayLog.getChapters().empty());
	const auto & chapter = gameHandler->gs->replayLog.getChapters().back();
	ASSERT_EQ(chapter.packs.size(), 1);
	EXPECT_EQ(chapter.packPayload(0), encoded);
}

TEST_F(PreparedBattleWorldCommitTest, ScalarLiveUpdateOracleMatchesCommittedCanonicalState)
{
	auto packet = defending();
	const auto original = first->save();
	battle()->updateUnit(first->unitId(), packet.changedStacks[0].data, 0);
	const auto expected = first->save();
	first->load(original); // finish scalar oracle BEFORE any private graph binding
	ASSERT_EQ(first->save(), original);
	auto owner = capture(packet);
	prepare(*owner);
	ASSERT_TRUE(owner->commitSimulation());
	EXPECT_EQ(first->save(), expected);
}

TEST_F(PreparedBattleWorldCommitTest, MissingLaterIdRejectsWithoutEarlierValueRootOrReplayPrefix)
{
	auto packet = defending();
	packet.changedStacks.emplace_back(999999, BattleChanges::EOperation::UPDATE);
	packet.changedStacks.back().data = JsonNode();
	const auto firstBefore = first->save(), secondBefore = second->save();
	const auto logBefore = replayBytes();
	const auto count = battle()->stacks.size();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_THROW(capture(packet), std::runtime_error);
	EXPECT_EQ(first->save(), firstBefore);
	EXPECT_EQ(second->save(), secondBefore);
	EXPECT_EQ(battle()->stacks.size(), count);
	EXPECT_EQ(replayBytes(), logBefore);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(PreparedBattleWorldCommitTest, MalformedLaterStateRejectsWithoutEarlierValueOrReplayPrefix)
{
	auto packet = defending();
	UnitChanges malformed(second->unitId(), BattleChanges::EOperation::UPDATE);
	malformed.data = second->save();
	malformed.data["state"]["guardianSpiritHitPoints"].Integer() = -1;
	packet.changedStacks.push_back(malformed);
	const auto firstBefore = first->save(), secondBefore = second->save();
	const auto logBefore = replayBytes();
	EXPECT_THROW(capture(packet), std::runtime_error);
	EXPECT_EQ(first->save(), firstBefore);
	EXPECT_EQ(second->save(), secondBefore);
	EXPECT_EQ(replayBytes(), logBefore);
}

TEST_F(PreparedBattleWorldCommitTest, IncompleteMutableAuthorityCannotPrepareOrCommitPrefix)
{
	auto owner = capture(defending());
	const auto logBefore = replayBytes();
	EXPECT_THROW(owner->prepare({}), std::invalid_argument);
	EXPECT_FALSE(owner->readyToCommit());
	EXPECT_FALSE(owner->commitSimulation());
	EXPECT_FALSE(first->defending);
	EXPECT_EQ(replayBytes(), logBefore);
}

TEST_F(PreparedBattleWorldCommitTest, ChangedGlobalGenerationRefusesAllParticipantsBeforeCas)
{
	auto owner = capture(defending());
	prepare(*owner);
	battle()->nodeHasChanged();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	const auto logBefore = replayBytes();
	EXPECT_FALSE(owner->readyToCommit());
	EXPECT_FALSE(owner->commitSimulation());
	EXPECT_FALSE(first->defending);
	EXPECT_EQ(replayBytes(), logBefore);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(PreparedBattleWorldCommitTest, ChangedSidecarRefusesWithoutValueOrReplayTransfer)
{
	auto owner = capture(defending());
	prepare(*owner);
	battle()->bloodrageFirstBloodUsed = !battle()->bloodrageFirstBloodUsed;
	const auto changed = battle()->bloodrageFirstBloodUsed;
	const auto logBefore = replayBytes();
	EXPECT_FALSE(owner->commitSimulation());
	EXPECT_FALSE(first->defending);
	EXPECT_EQ(battle()->bloodrageFirstBloodUsed, changed);
	EXPECT_EQ(replayBytes(), logBefore);
}

TEST_F(PreparedBattleWorldCommitTest, ChangedReplayReservationRefusesBeforeGraphAndValues)
{
	auto owner = capture(defending());
	prepare(*owner);
	gameHandler->gs->replayLog.reconfigureOnLoad(1);
	const auto logBefore = replayBytes();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_FALSE(owner->commitSimulation());
	EXPECT_FALSE(first->defending);
	EXPECT_EQ(replayBytes(), logBefore);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(PreparedBattleWorldCommitTest, ChangedPhysicalRootAllocationRefusesBeforeAnyParticipantWrite)
{
	auto owner = capture(defending());
	prepare(*owner);
	battle()->stacks.reserve(battle()->stacks.capacity() + 1);
	const auto * changed = battle()->stacks.data();
	const auto logBefore = replayBytes();
	EXPECT_FALSE(owner->commitSimulation());
	EXPECT_FALSE(first->defending);
	EXPECT_EQ(battle()->stacks.data(), changed);
	EXPECT_EQ(replayBytes(), logBefore);
}

TEST_F(PreparedBattleWorldCommitTest, ChangedRootBeforePreparationCannotBeRecapturedByParticipantConstruction)
{
	auto owner = capture(defending());
	battle()->stacks.reserve(battle()->stacks.capacity() + 1);
	const auto * changed = battle()->stacks.data();
	const auto logBefore = replayBytes();
	EXPECT_THROW(prepare(*owner), std::runtime_error);
	EXPECT_FALSE(owner->commitSimulation());
	EXPECT_FALSE(first->defending);
	EXPECT_EQ(battle()->stacks.data(), changed);
	EXPECT_EQ(replayBytes(), logBefore);
}

TEST_F(PreparedBattleWorldCommitTest, ChangedWorldDayRefusesWithoutReinterpretingSnapshot)
{
	auto owner = capture(defending());
	prepare(*owner);
	gameHandler->gs->day += 1;
	const auto logBefore = replayBytes();
	EXPECT_FALSE(owner->commitSimulation());
	EXPECT_FALSE(first->defending);
	EXPECT_EQ(replayBytes(), logBefore);
}

TEST_F(PreparedBattleWorldCommitTest, DisabledRecordingStillCommitsStateWithoutInventingReplay)
{
	gameHandler->gs->replayLog = ReplayLog();
	const auto logBefore = replayBytes();
	auto owner = capture(defending());
	prepare(*owner);
	ASSERT_TRUE(owner->commitSimulation());
	EXPECT_TRUE(first->defending);
	EXPECT_EQ(replayBytes(), logBefore);
}

TEST_F(PreparedBattleWorldCommitTest, DisabledReplayTimeStopIgnoresMalformedMinimumDeltaBeforeLoad)
{
	gameHandler->gs->replayLog = ReplayLog();
	first->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID()));
	const auto before = first->save();
	BattleUnitsChanged packet;
	packet.battleID = battle()->getBattleID();
	UnitChanges change(first->unitId(), BattleChanges::EOperation::UPDATE);
	change.healthDelta = std::numeric_limits<int64_t>::min();
	change.data = JsonNode("not a state object");
	packet.changedStacks.push_back(change);
	auto owner = capture(packet);
	prepare(*owner);
	ASSERT_TRUE(owner->commitSimulation());
	EXPECT_EQ(first->save(), before);
}

TEST_F(PreparedBattleWorldCommitTest, DisabledReplayOccupiedResurrectionIgnoresMalformedPayloadBeforeLoad)
{
	gameHandler->gs->replayLog = ReplayLog();
	auto damage = first->getAvailableHealth();
	first->damage(damage);
	addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 1);
	const auto before = first->save();
	BattleUnitsChanged packet;
	packet.battleID = battle()->getBattleID();
	UnitChanges change(first->unitId(), BattleChanges::EOperation::UPDATE);
	change.healthDelta = 1;
	change.data = JsonNode("malformed");
	packet.changedStacks.push_back(change);
	auto owner = capture(packet);
	prepare(*owner);
	ASSERT_TRUE(owner->commitSimulation());
	EXPECT_EQ(first->save(), before);
}

TEST_F(PreparedBattleWorldCommitTest, EmptyChapterSnapshotIsPreparedBeforeStateAndPublishedWithPack)
{
	gameHandler->gs->replayLog = ReplayLog();
	gameHandler->gs->replayLog.configure(false, 2);
	auto packet = defending();
	const auto encoded = ReplayPackSerializer::write(packet);
	auto owner = capture(packet);
	EXPECT_TRUE(gameHandler->gs->replayLog.empty());
	EXPECT_FALSE(first->defending);
	prepare(*owner);
	ASSERT_TRUE(owner->commitSimulation());
	const auto & chapters = gameHandler->gs->replayLog.getChapters();
	ASSERT_EQ(chapters.size(), 1);
	EXPECT_FALSE(chapters[0].gamestateSnapshot.empty());
	EXPECT_EQ(chapters[0].snapshotVersion, ESerializationVersion::CURRENT);
	ASSERT_EQ(chapters[0].packs.size(), 1);
	EXPECT_EQ(chapters[0].packPayload(0), encoded);
}

TEST_F(PreparedBattleWorldCommitTest, AddThenRemoveKeepsNativePhysicalOccurrenceAndQueryOnlyExclusion)
{
	BattleUnitsChanged packet;
	packet.battleID = battle()->getBattleID();
	const auto index = battle()->stacks.size();
	const auto id = battle()->nextUnitId();
	UnitChanges add(id, BattleChanges::EOperation::ADD);
	add.data = addition();
	add.data["rebirthOriginalAggregateHP"].Integer() = 1;
	packet.changedStacks.push_back(add);
	packet.changedStacks.emplace_back(id, BattleChanges::EOperation::REMOVE);
	JsonNode expected;
	{
		battle::ProjectedBattleWorldArena oracle(*battle());
		oracle.applyPacket(packet);
		expected = oracle.snapshotAt(index);
	} // Retire the query oracle before binding the commit owner's graph.
	auto owner = capture(packet);
	const auto * native = &owner->projected().originalAt(index);
	ASSERT_TRUE(owner->projected().unitAt(index).isGhost());
	ASSERT_EQ(owner->projected().retainedQueryCount(), 1);
	prepare(*owner);
	ASSERT_TRUE(owner->commitSimulation());
	ASSERT_EQ(battle()->stacks.size(), index + 1);
	EXPECT_EQ(battle()->stacks[index].get(), native);
	EXPECT_EQ(battle()->stacks[index]->save(), expected);
	EXPECT_EQ(native->getBattle(), battle());
	EXPECT_TRUE(native->isGhost());
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	owner.reset(); // graph/query owners retire while the native root remains alive
	EXPECT_EQ(battle()->stacks[index]->save(), expected);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(PreparedBattleWorldCommitTest, RepeatedIdIntermediateFormsKeepFinalHealthAndNativeSource)
{
	const auto base = first->save();
	first->beginBattleForm(CreatureID(2), 3);
	const auto form = first->save();
	first->endBattleForm();
	first->load(base);
	ASSERT_EQ(first->save(), base);
	BattleUnitsChanged packet;
	packet.battleID = battle()->getBattleID();
	for(const auto & json : {form, base, form})
	{
		UnitChanges change(first->unitId(), BattleChanges::EOperation::UPDATE);
		change.data = json;
		packet.changedStacks.push_back(change);
	}
	auto owner = capture(packet);
	prepare(*owner);
	ASSERT_TRUE(owner->commitSimulation());
	EXPECT_EQ(first->save(), form);
	EXPECT_TRUE(vstd::contains(first->getParentNodes(), CreatureID(2).toCreature()));
	EXPECT_EQ(first->doubleWide(), CreatureID(2).toCreature()->isDoubleWide());
}

TEST_F(PreparedBattleWorldCommitTest, PositionAndProtectSidecarTransferTogetherWithoutChangingIssuance)
{
	auto * protectedUnit = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex + 2), 4);
	HeroOrderState order;
	order.command = HeroCommand::PROTECT;
	order.issuedRound = 1;
	order.primaryTargetUnitId = first->unitId();
	order.secondaryTargetUnitId = protectedUnit->unitId();
	battle()->setHeroOrderStates(BattleSide::ATTACKER, {order});
	auto packet = defending();
	packet.changedStacks[0].data["state"]["position"].Integer() = leftHex - 4;
	auto owner = capture(packet);
	const auto expected = owner->projected().orderStates(BattleSide::ATTACKER);
	ASSERT_EQ(expected.size(), 1);
	ASSERT_TRUE(expected[0].protectBroken);
	prepare(*owner);
	ASSERT_TRUE(owner->commitSimulation());
	EXPECT_EQ(first->position, BattleHex(leftHex - 4));
	EXPECT_EQ(battle()->getHeroOrderStates(BattleSide::ATTACKER), expected);
}

TEST_F(PreparedBattleWorldCommitTest, SingleUseRetainsActualWorldAndClosesPrivateQueries)
{
	const auto priorOwners = gameHandler->gs.use_count();
	auto owner = capture(defending());
	// Data retains the world through state-lock release; the root pin independently
	// retains it through root-lock release. Both owners retire on owner.reset().
	EXPECT_EQ(gameHandler->gs.use_count(), priorOwners + 2);
	prepare(*owner);
	EXPECT_THROW(owner->projected(), std::logic_error);
	ASSERT_TRUE(owner->commitSimulation());
	const auto final = first->save();
	const auto log = replayBytes();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_FALSE(owner->readyToCommit());
	EXPECT_FALSE(owner->commitSimulation());
	EXPECT_EQ(first->save(), final);
	EXPECT_EQ(replayBytes(), log);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	owner.reset();
	EXPECT_EQ(gameHandler->gs.use_count(), priorOwners);
}

TEST_F(PreparedBattleWorldCommitTest, MissingOrForeignGuardAndAbsentBattleRefuseBeforeReplayPreparation)
{
	auto packet = defending();
	const auto before = replayBytes();
	EXPECT_THROW(Owner(gameHandler->gs, std::unique_lock<std::shared_mutex>(), packet), std::invalid_argument);
	std::shared_mutex unrelated;
	EXPECT_THROW(Owner(gameHandler->gs, std::unique_lock<std::shared_mutex>(unrelated), packet), std::invalid_argument);
	packet.battleID = BattleID(999999);
	EXPECT_THROW(capture(packet), std::invalid_argument);
	auto empty = std::make_shared<CGameState>(); // day is intentionally not read before missing-battle refusal
	EXPECT_THROW(Owner(empty, std::unique_lock<std::shared_mutex>(CGameState::mutex), packet), std::invalid_argument);
	EXPECT_EQ(replayBytes(), before);
	EXPECT_FALSE(first->defending);
}

class LegacyPreparedBattleWorldCommitTest : public PreparedBattleWorldCommitTest
{
protected:
	BattleUnitsChanged duplicateRemoval()
	{
		BattleUnitsChanged packet;
		packet.battleID = battle()->getBattleID();
		UnitChanges add(first->unitId(), BattleChanges::EOperation::ADD);
		add.data = addition();
		packet.changedStacks.push_back(add);
		packet.changedStacks.emplace_back(first->unitId(), BattleChanges::EOperation::REMOVE);
		return packet;
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		JsonNode rules;
		rules.Struct();
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, rules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode());
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode());
	}
};

TEST_F(LegacyPreparedBattleWorldCommitTest, NativeDuplicateIdsPacketRefusesAtCrisisValidation)
{
	ASSERT_FALSE(heroCommands::supportedByRules(battle()->getHeroCommandRules(), HeroCommand::FOCUS_FIRE));
	auto packet = duplicateRemoval();
	// This fixture is retired before any other test creates a projection lease.
	// The native packet may already have a physical/replay prefix: do not assert rollback.
	try
	{
		gameHandler->gs->apply(packet);
		FAIL() << "Native duplicate packet unexpectedly accepted";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_STREQ(error.what(), "Crisis Command requires unique valid unit descriptors");
	}
}

TEST_F(LegacyPreparedBattleWorldCommitTest, DuplicateIdsRefuseBeforePreparedWorldValueRootOrReplayPrefix)
{
	ASSERT_FALSE(heroCommands::supportedByRules(battle()->getHeroCommandRules(), HeroCommand::FOCUS_FIRE));
	const auto packet = duplicateRemoval();
	const auto firstBefore = first->save(), secondBefore = second->save();
	const auto logBefore = replayBytes();
	std::vector<const CStack *> originalRoots;
	for(const auto & unit : battle()->stacks)
		originalRoots.push_back(unit.get());
	const auto * rootData = battle()->stacks.data();
	const auto rootCapacity = battle()->stacks.capacity();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	try
	{
		capture(packet);
		FAIL() << "Prepared duplicate packet unexpectedly accepted";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_STREQ(error.what(), "Crisis Command requires unique valid unit descriptors");
	}
	ASSERT_EQ(battle()->stacks.size(), originalRoots.size());
	EXPECT_EQ(battle()->stacks.data(), rootData);
	EXPECT_EQ(battle()->stacks.capacity(), rootCapacity);
	for(size_t i = 0; i < originalRoots.size(); ++i)
		EXPECT_EQ(battle()->stacks[i].get(), originalRoots[i]);
	EXPECT_EQ(first->save(), firstBefore);
	EXPECT_EQ(second->save(), secondBefore);
	EXPECT_EQ(battle()->getStack(first->unitId(), false), first);
	EXPECT_EQ(replayBytes(), logBefore);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}
}
