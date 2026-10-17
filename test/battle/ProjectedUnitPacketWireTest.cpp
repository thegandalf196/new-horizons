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
	CStack * chainSource = nullptr;
	BattleUnitsChanged chainPacket;
	void TearDown() override
	{
		arena.reset();
		NewHorizonsRebirthChainFixture::TearDown();
	}
	void prepareChain()
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		chainSource = firstReborn();
		ASSERT_NE(chainSource, nullptr);
		auto lethal = chainSource->getAvailableHealth();
		chainSource->damage(lethal); // setup only: no reaction dispatcher
		ASSERT_FALSE(chainSource->alive());
		chainPacket = candidate(chainSource); // maintained exact scalar spawn recipe
	}
	void prepareSpark(bool selected = true)
	{
		startGame();
		defenderSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
		defenderSideHero->clearSlots();
		attackerSideHero->clearSlots();
		const SecondarySkill skill(SecondarySkill::decode(SKILL));
		defenderSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		defenderSideHero->applyPerkSelection({SKILL, "new-horizons:elementalRebirth.primalBurst"});
		defenderSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		defenderSideHero->applyPerkSelection({SKILL, CHAIN});
		defenderSideHero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		if(selected)
			defenderSideHero->applyPerkSelection({SKILL, "new-horizons:elementalRebirth.phoenixSpark"});
		ASSERT_EQ(defenderSideHero->hasActivePerk(SKILL, "new-horizons:elementalRebirth.phoenixSpark"), selected);
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creature("core:angel"), 3));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creature("core:pikeman"), 100));
		startBattle();
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit->unitSide() == BattleSide::DEFENDER && unit->unitSlot() == SlotID(0))
				source = battle()->getStack(unit->unitId(), false);
		ASSERT_NE(source, nullptr);
		const auto category = battle()->getCreatureCategoryRules().lookup(source->unitType()->getJsonKey());
		ASSERT_TRUE(category);
		ASSERT_EQ(category->category, newHorizonsCreatures::CreatureCategory::CHAMPION);
		auto lethal = source->getAvailableHealth();
		source->damage(lethal);
		ASSERT_FALSE(source->alive());
	}
	BattleUnitsChanged spark() const
	{
		BattleUnitsChanged packet;
		packet.battleID = BattleID(0);
		packet.phoenixSparkConsumption = newHorizonsElementalRebirth::PhoenixSparkConsumption{
			BattleSide::DEFENDER, source->unitId()};
		return packet;
	}
	BattleUnitsChanged rollback() const
	{
		BattleUnitsChanged packet;
		packet.battleID = BattleID(0);
		packet.rebirthChainConsumption = chainPacket.rebirthChainConsumption;
		packet.rebirthChainConsumption->rollback = true;
		packet.changedStacks.emplace_back(packet.rebirthChainConsumption->spawnUnitId, UnitChanges::EOperation::REMOVE);
		return packet;
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

TEST_F(ProjectedUnitPacketWireTest, ChainAddConsumesPrivateTokenAndKeepsQueryOutsideRoster)
{
	ASSERT_NO_FATAL_FAILURE(prepareChain());
	capture();
	const auto size = arena->size();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	arena->applyPacket(chainPacket);
	EXPECT_TRUE(arena->rebirthChainUsed(BattleSide::DEFENDER));
	EXPECT_FALSE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
	EXPECT_EQ(arena->size(), size + 1);
	EXPECT_EQ(arena->retainedQueryCount(), 1);
	EXPECT_EQ(arena->findUnit(std::numeric_limits<uint32_t>::max()), nullptr);
	ASSERT_NE(arena->findUnit(chainPacket.changedStacks.front().id), nullptr);
	EXPECT_TRUE(arena->findUnit(chainPacket.changedStacks.front().id)->alive());
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(ProjectedUnitPacketWireTest, ChainAddCompleteValuesAndSidecarsMatchActualVisitor)
{
	ASSERT_NO_FATAL_FAILURE(prepareChain());
	scalarParity({chainPacket});
}

TEST_F(ProjectedUnitPacketWireTest, ChainRollbackUsesLatestPrivateOutputBeforeRestoringToken)
{
	ASSERT_NO_FATAL_FAILURE(prepareChain());
	capture();
	arena->applyPacket(chainPacket);
	arena->applyPacket(rollback());
	EXPECT_FALSE(arena->rebirthChainUsed(BattleSide::DEFENDER));
	ASSERT_NE(arena->findUnit(chainPacket.changedStacks.front().id), nullptr);
	EXPECT_TRUE(arena->findUnit(chainPacket.changedStacks.front().id)->isGhost());
	EXPECT_FALSE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
}

TEST_F(ProjectedUnitPacketWireTest, ChainAddThenRollbackMatchesBothScalarPackets)
{
	ASSERT_NO_FATAL_FAILURE(prepareChain());
	scalarParity({chainPacket, rollback()});
}

TEST_F(ProjectedUnitPacketWireTest, DuplicateConsumptionRefusesAndPoisonsWithoutLivePrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepareChain());
	const auto originalSize = battle()->stacks.size();
	capture();
	arena->applyPacket(chainPacket);
	EXPECT_THROW(arena->applyPacket(chainPacket), std::runtime_error);
	EXPECT_THROW(arena->size(), std::logic_error);
	EXPECT_EQ(battle()->stacks.size(), originalSize);
	EXPECT_FALSE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
}

TEST_F(ProjectedUnitPacketWireTest, MalformedCountIsRejectedBeforeAnyLiveTokenOrOutput)
{
	ASSERT_NO_FATAL_FAILURE(prepareChain());
	chainPacket.changedStacks.front().data["count"].Integer() += 1;
	capture();
	EXPECT_THROW(arena->applyPacket(chainPacket), std::runtime_error);
	EXPECT_FALSE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
	EXPECT_EQ(battle()->getStack(chainPacket.changedStacks.front().id, false), nullptr);
	EXPECT_THROW(arena->size(), std::logic_error);
}

TEST_F(ProjectedUnitPacketWireTest, RollbackCannotRestoreUnspentToken)
{
	ASSERT_NO_FATAL_FAILURE(prepareChain());
	capture();
	EXPECT_THROW(arena->applyPacket(rollback()), std::runtime_error);
	EXPECT_FALSE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
}

TEST_F(ProjectedUnitPacketWireTest, ShapeRejectsWrongOutputOperationBeforePrivateAdd)
{
	ASSERT_NO_FATAL_FAILURE(prepareChain());
	chainPacket.changedStacks.front().operation = UnitChanges::EOperation::UPDATE;
	capture();
	EXPECT_THROW(arena->applyPacket(chainPacket), std::runtime_error);
	EXPECT_FALSE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
}

TEST_F(ProjectedUnitPacketWireTest, ChainWireRoundtripPreservesVersionedMetadata)
{
	ASSERT_NO_FATAL_FAILURE(prepareChain());
	CMemorySerializer writer;
	writer.oser & chainPacket;
	const auto bytes = writer.extractBuffer();
	CMemorySerializer reader(bytes);
	BattleUnitsChanged restored;
	reader.iser & restored;
	EXPECT_EQ(wire(restored), bytes);
	ASSERT_TRUE(restored.rebirthChainConsumption);
	EXPECT_EQ(restored.rebirthChainConsumption->sourceUnitId, chainSource->unitId());
	scalarParity({restored});
}

TEST_F(ProjectedUnitPacketWireTest, OlderWireCannotDiscardChainMetadataBeforeFirstByte)
{
	ASSERT_NO_FATAL_FAILURE(prepareChain());
	CMemorySerializer writer;
	writer.oser.version = ESerializationVersion::NEW_HORIZONS_LEARNING_MASTER_TEACHER;
	EXPECT_THROW(writer.oser & chainPacket, std::runtime_error);
	EXPECT_TRUE(writer.extractBuffer().empty());
}

TEST_F(ProjectedUnitPacketWireTest, PhoenixReceiptConsumesOnlyItsOwnSideWithoutSpawning)
{
	ASSERT_NO_FATAL_FAILURE(prepareSpark());
	capture();
	const auto size = arena->size();
	arena->applyPacket(spark());
	EXPECT_TRUE(arena->phoenixSparkUsed(BattleSide::DEFENDER));
	EXPECT_FALSE(arena->phoenixSparkUsed(BattleSide::ATTACKER));
	EXPECT_FALSE(arena->rebirthChainUsed(BattleSide::DEFENDER));
	EXPECT_FALSE(battle()->getPhoenixSparkUsed(BattleSide::DEFENDER));
	EXPECT_EQ(arena->size(), size);
	EXPECT_EQ(arena->retainedQueryCount(), 0);
}

TEST_F(ProjectedUnitPacketWireTest, PhoenixReceiptAndOuterSidecarsMatchActualVisitor)
{
	ASSERT_NO_FATAL_FAILURE(prepareSpark());
	scalarParity({spark()});
}

TEST_F(ProjectedUnitPacketWireTest, PhoenixSecondUsePoisonsPrivateOwnerButNeverSpendsLiveUse)
{
	ASSERT_NO_FATAL_FAILURE(prepareSpark());
	capture();
	arena->applyPacket(spark());
	EXPECT_THROW(arena->applyPacket(spark()), std::runtime_error);
	EXPECT_THROW(arena->size(), std::logic_error);
	EXPECT_FALSE(battle()->getPhoenixSparkUsed(BattleSide::DEFENDER));
}

TEST_F(ProjectedUnitPacketWireTest, UnselectedPhoenixReceiptIsRejected)
{
	ASSERT_NO_FATAL_FAILURE(prepareSpark(false));
	capture();
	EXPECT_THROW(arena->applyPacket(spark()), std::runtime_error);
	EXPECT_FALSE(battle()->getPhoenixSparkUsed(BattleSide::DEFENDER));
}

TEST_F(ProjectedUnitPacketWireTest, PhoenixMustBeSeparateFromOrdinaryEntries)
{
	ASSERT_NO_FATAL_FAILURE(prepareSpark());
	auto packet = spark();
	packet.changedStacks.emplace_back(source->unitId(), UnitChanges::EOperation::REMOVE);
	capture();
	EXPECT_THROW(arena->applyPacket(packet), std::runtime_error);
	EXPECT_FALSE(source->isGhost());
	EXPECT_FALSE(battle()->getPhoenixSparkUsed(BattleSide::DEFENDER));
}

TEST_F(ProjectedUnitPacketWireTest, OlderWireCannotDiscardPhoenixReceiptBeforeFirstByte)
{
	ASSERT_NO_FATAL_FAILURE(prepareSpark());
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
