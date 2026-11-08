/*
 * ProjectedUnitPacketWireTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../server/battles/NewHorizonsRebirthChainFixture.h"
#include "../../lib/battle/ProjectedBattleWorldArena.h"
#include "../../lib/battle/ElementalConsumptionContext.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <limits>

namespace
{
using Arena = battle::ProjectedBattleWorldArena;
template<typename T> std::vector<std::byte> wire(T value)
{
	CMemorySerializer writer;
	writer.oser & value;
	return writer.extractBuffer();
}

class ProjectedUnitPacketWireTest : public NewHorizonsRebirthChainFixture
{
protected:
	std::unique_ptr<Arena> arena;
	CStack * deadSource = nullptr;
	BattleUnitsChanged summon;
	void TearDown() override
	{
		arena.reset();
		NewHorizonsRebirthChainFixture::TearDown();
	}
	void prepareSummon(bool vigilance = true)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(vigilance));
		ASSERT_FALSE(newHorizonsElementalRebirth::activeProfile(defenderSideHero));
		deadSource = source;
		auto lethal = deadSource->getAvailableHealth();
		deadSource->damage(lethal); // Fixture setup only; no retired reaction is dispatched.
		ASSERT_FALSE(deadSource->alive());
		summon = retiredCandidate();
		summon.rebirthChainConsumption.reset(); // Same ordinary UnitInfo, independent of retired effects.
	}
	BattleUnitsChanged chain() const
	{
		auto packet = summon;
		packet.rebirthChainConsumption = newHorizonsElementalRebirth::ChainConsumption{
			BattleSide::DEFENDER, deadSource->unitId(), summon.changedStacks.front().id};
		return packet;
	}
	BattleUnitsChanged spark() const
	{
		BattleUnitsChanged packet;
		packet.battleID = BattleID(0);
		packet.phoenixSparkConsumption = newHorizonsElementalRebirth::PhoenixSparkConsumption{
			BattleSide::DEFENDER, deadSource->unitId()};
		return packet;
	}
	BattleUnitsChanged rollback() const
	{
		auto packet = chain();
		packet.rebirthChainConsumption->rollback = true;
		packet.changedStacks.clear();
		packet.changedStacks.emplace_back(summon.changedStacks.front().id, UnitChanges::EOperation::REMOVE);
		return packet;
	}
	BattleUnitsChanged removeSummon() const
	{
		BattleUnitsChanged packet;
		packet.battleID = BattleID(0);
		packet.changedStacks.emplace_back(summon.changedStacks.front().id, UnitChanges::EOperation::REMOVE);
		return packet;
	}
	void rejectsBoth(const BattleUnitsChanged & packet)
	{
		std::vector<JsonNode> before;
		for(const auto & unit : battle()->stacks) before.push_back(unit->save());
		const auto command = battle()->getCommandResources(BattleSide::DEFENDER);
		const auto allowance = battle()->getHeroActionAllowances(BattleSide::DEFENDER);
		capture();
		EXPECT_THROW(arena->applyPacket(packet), std::runtime_error);
		EXPECT_THROW(arena->size(), std::logic_error);
		arena.reset();
		auto scalarPacket = packet;
		EXPECT_THROW(gameState()->apply(scalarPacket), std::runtime_error);
		ASSERT_EQ(battle()->stacks.size(), before.size());
		for(size_t i = 0; i < before.size(); ++i) EXPECT_EQ(battle()->stacks[i]->save(), before[i]);
		EXPECT_EQ(battle()->getCommandResources(BattleSide::DEFENDER), command);
		EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::DEFENDER), allowance);
		EXPECT_FALSE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
		EXPECT_FALSE(battle()->getPhoenixSparkUsed(BattleSide::DEFENDER));
	}
	void capture() { arena = std::make_unique<Arena>(*battle()); }
	void scalarParity(const std::vector<BattleUnitsChanged> & packets)
	{
		capture();
		const auto generation = PreparedBonusGraphStorage::currentGeneration();
		const auto originalChain = battle()->getRebirthChainUsed(BattleSide::DEFENDER);
		const auto originalSpark = battle()->getPhoenixSparkUsed(BattleSide::DEFENDER);
		for(const auto & packet : packets)
			arena->applyPacket(packet);
		std::vector<JsonNode> expected;
		for(size_t i = 0; i < arena->size(); ++i)
			expected.push_back(arena->snapshotAt(i));
		const auto crisis = wire(arena->crisisState());
		const auto deaths = arena->bloodrageDeaths();
		const auto chain = arena->rebirthChainUsed(BattleSide::DEFENDER);
		const auto sparkUsed = arena->phoenixSparkUsed(BattleSide::DEFENDER);
		EXPECT_EQ(battle()->getRebirthChainUsed(BattleSide::DEFENDER), originalChain);
		EXPECT_EQ(battle()->getPhoenixSparkUsed(BattleSide::DEFENDER), originalSpark);
		EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
		arena.reset(); // retire projected graph and query identities before scalar mutation
		for(auto packet : packets)
			gameState()->apply(packet); // real shared-state visitor, no transport claim
		ASSERT_EQ(battle()->stacks.size(), expected.size());
		for(size_t i = 0; i < expected.size(); ++i)
		{
			EXPECT_EQ(battle()->stacks[i]->save(), expected[i]);
		}
		EXPECT_EQ(wire(battle()->getCrisisCommandState()), crisis);
		EXPECT_EQ(battle()->bloodrageDestroyedUnits, deaths);
		EXPECT_EQ(battle()->getRebirthChainUsed(BattleSide::DEFENDER), chain);
		EXPECT_EQ(battle()->getPhoenixSparkUsed(BattleSide::DEFENDER), sparkUsed);
	}
};

TEST_F(ProjectedUnitPacketWireTest, IndependentSummonKeepsQueryOutsideRosterWithoutRetiredTokens)
{
	ASSERT_NO_FATAL_FAILURE(prepareSummon());
	capture();
	const auto size = arena->size();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	arena->applyPacket(summon);
	EXPECT_FALSE(arena->rebirthChainUsed(BattleSide::DEFENDER));
	EXPECT_FALSE(arena->phoenixSparkUsed(BattleSide::DEFENDER));
	EXPECT_EQ(arena->size(), size + 1);
	EXPECT_EQ(arena->retainedQueryCount(), 0); // Ordinary ADD has no retired Rebirth HP-capacity query.
	EXPECT_EQ(arena->findUnit(std::numeric_limits<uint32_t>::max()), nullptr);
	ASSERT_NE(arena->findUnit(summon.changedStacks.front().id), nullptr);
	EXPECT_TRUE(arena->findUnit(summon.changedStacks.front().id)->alive());
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(ProjectedUnitPacketWireTest, IndependentSummonCompleteValuesAndSidecarsMatchActualVisitor)
{
	ASSERT_NO_FATAL_FAILURE(prepareSummon());
	scalarParity({summon});
}

TEST_F(ProjectedUnitPacketWireTest, IndependentRemoveUsesLatestPrivateOutputWithoutRetiredToken)
{
	ASSERT_NO_FATAL_FAILURE(prepareSummon());
	capture();
	arena->applyPacket(summon);
	arena->applyPacket(removeSummon());
	EXPECT_FALSE(arena->rebirthChainUsed(BattleSide::DEFENDER));
	ASSERT_NE(arena->findUnit(summon.changedStacks.front().id), nullptr);
	EXPECT_TRUE(arena->findUnit(summon.changedStacks.front().id)->isGhost());
	EXPECT_FALSE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
}

TEST_F(ProjectedUnitPacketWireTest, IndependentSummonThenRemoveMatchesBothScalarPackets)
{
	ASSERT_NO_FATAL_FAILURE(prepareSummon());
	scalarParity({summon, removeSummon()});
}

TEST_F(ProjectedUnitPacketWireTest, DuplicateIndependentSummonPoisonsWithoutLivePrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepareSummon());
	const auto roots = battle()->stacks.size();
	capture();
	arena->applyPacket(summon);
	EXPECT_THROW(arena->applyPacket(summon), std::runtime_error);
	EXPECT_THROW(arena->size(), std::logic_error);
	EXPECT_EQ(battle()->stacks.size(), roots);
	EXPECT_FALSE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
}

TEST_F(ProjectedUnitPacketWireTest, RetiredChainCannotAddPrivateOrScalarOutput)
{
	ASSERT_NO_FATAL_FAILURE(prepareSummon());
	rejectsBoth(chain());
}

TEST_F(ProjectedUnitPacketWireTest, MalformedRetiredCountCannotBypassCurrentRefusal)
{
	ASSERT_NO_FATAL_FAILURE(prepareSummon());
	auto packet = chain();
	packet.changedStacks.front().data["count"].Integer() += 1;
	rejectsBoth(packet); // Exact historical computed-HP admission is retired, not asserted here.
}

TEST_F(ProjectedUnitPacketWireTest, RetiredRollbackCannotRestoreUnspentToken)
{
	ASSERT_NO_FATAL_FAILURE(prepareSummon());
	rejectsBoth(rollback());
}

TEST_F(ProjectedUnitPacketWireTest, RetiredShapeRejectsWrongOutputOperationBeforePrivateAdd)
{
	ASSERT_NO_FATAL_FAILURE(prepareSummon());
	auto packet = chain();
	packet.changedStacks.front().operation = UnitChanges::EOperation::UPDATE;
	rejectsBoth(packet);
}

TEST_F(ProjectedUnitPacketWireTest, ReservedChainWireRoundtripDoesNotAuthorizeCurrentConsumption)
{
	ASSERT_NO_FATAL_FAILURE(prepareSummon());
	auto packet = chain();
	CMemorySerializer writer;
	writer.oser & packet;
	const auto bytes = writer.extractBuffer();
	CMemorySerializer reader(bytes);
	BattleUnitsChanged restored;
	reader.iser & restored;
	EXPECT_EQ(wire(restored), bytes);
	ASSERT_TRUE(restored.rebirthChainConsumption);
	EXPECT_EQ(restored.rebirthChainConsumption->sourceUnitId, deadSource->unitId());
	rejectsBoth(restored);
}

TEST_F(ProjectedUnitPacketWireTest, OlderWireCannotDiscardChainMetadataBeforeFirstByte)
{
	ASSERT_NO_FATAL_FAILURE(prepareSummon());
	auto packet = chain();
	CMemorySerializer writer;
	writer.oser.version = ESerializationVersion::NEW_HORIZONS_LEARNING_MASTER_TEACHER;
	EXPECT_THROW(writer.oser & packet, std::runtime_error);
	EXPECT_TRUE(writer.extractBuffer().empty());
}

TEST_F(ProjectedUnitPacketWireTest, RetiredPhoenixCannotConsumeEitherPrivateOrScalarSide)
{
	ASSERT_NO_FATAL_FAILURE(prepareSummon());
	rejectsBoth(spark());
}

TEST_F(ProjectedUnitPacketWireTest, ReservedPhoenixWireRoundtripCannotPublishOuterSidecars)
{
	ASSERT_NO_FATAL_FAILURE(prepareSummon());
	auto packet = spark();
	const auto bytes = wire(packet);
	CMemorySerializer reader(bytes);
	BattleUnitsChanged restored;
	reader.iser & restored;
	EXPECT_EQ(wire(restored), bytes);
	rejectsBoth(restored);
}

TEST_F(ProjectedUnitPacketWireTest, RepeatedRetiredPhoenixCannotSpendLiveUse)
{
	ASSERT_NO_FATAL_FAILURE(prepareSummon());
	rejectsBoth(spark());
	rejectsBoth(spark()); // Fresh projected owners still cannot turn rejected metadata into a token.
}

TEST_F(ProjectedUnitPacketWireTest, NoVigilanceSelectionAlsoCannotAuthorizeRetiredPhoenix)
{
	ASSERT_NO_FATAL_FAILURE(prepareSummon(false));
	rejectsBoth(spark());
}

TEST_F(ProjectedUnitPacketWireTest, RetiredPhoenixMustRemainSeparateFromOrdinaryEntries)
{
	ASSERT_NO_FATAL_FAILURE(prepareSummon());
	auto packet = spark();
	packet.changedStacks.emplace_back(deadSource->unitId(), UnitChanges::EOperation::REMOVE);
	rejectsBoth(packet);
}

TEST_F(ProjectedUnitPacketWireTest, OlderWireCannotDiscardPhoenixReceiptBeforeFirstByte)
{
	ASSERT_NO_FATAL_FAILURE(prepareSummon());
	auto packet = spark();
	CMemorySerializer writer;
	writer.oser.version = ESerializationVersion::NEW_HORIZONS_LEARNING_MASTER_TEACHER;
	EXPECT_THROW(writer.oser & packet, std::runtime_error);
	EXPECT_TRUE(writer.extractBuffer().empty());
}

TEST_F(ProjectedUnitPacketWireTest, BoundBattleIdentityCannotBeRelabelled)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	BattleUnitsChanged packet;
	packet.battleID = BattleID(123);
	capture();
	EXPECT_THROW(arena->applyPacket(packet), std::invalid_argument);
	EXPECT_THROW(arena->size(), std::logic_error);
}

TEST_F(ProjectedUnitPacketWireTest, OrdinaryEnvelopeUsesSameSharedEntryProgram)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	BattleUnitsChanged packet;
	packet.battleID = BattleID(0);
	packet.changedStacks.emplace_back(99999, UnitChanges::EOperation::REMOVE);
	scalarParity({packet});
}
}
