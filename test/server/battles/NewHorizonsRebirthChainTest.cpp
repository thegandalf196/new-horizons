/*
 * NewHorizonsRebirthChainTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "NewHorizonsRebirthChainFixture.h"
#include "../../../lib/serializer/CMemorySerializer.h"

class NewHorizonsRebirthChainTest : public NewHorizonsRebirthChainFixture {};

TEST_F(NewHorizonsRebirthChainTest, SecondAppearanceUsesOriginalHPAndCannotRebirthAgain)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * first = firstReborn();
	ASSERT_NE(first, nullptr);
	const auto originalHP = first->getRebirthOriginalAggregateHP();
	ASSERT_GT(originalHP, 1);
	injure(first, 1);
	EXPECT_EQ(first->getRebirthOriginalAggregateHP(), originalHP);
	const auto firstId = first->unitId();
	injure(first, first->getAvailableHealth());
	EXPECT_TRUE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
	CStack * second = nullptr;
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit->alive() && unit->isSummoned() && unit->unitId() != firstId)
			second = battle()->getStack(unit->unitId(), false);
	ASSERT_NE(second, nullptr);
	EXPECT_EQ(second->getAvailableHealth(), originalHP / 4);
	EXPECT_EQ(second->getRebirthOriginalAggregateHP(), 0);
	EXPECT_TRUE(second->isSummoned());
	EXPECT_FALSE(newHorizonsElementalRebirth::captureDeathSource(*second, defenderSideHero));
	injure(second, second->getAvailableHealth());
	for(const auto * unit : battle()->battleGetAllUnits(false))
		EXPECT_FALSE(unit->alive() && unit->isSummoned());
	EXPECT_TRUE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
	EXPECT_FALSE(battle()->getRebirthChainUsed(BattleSide::ATTACKER));
}

TEST_F(NewHorizonsRebirthChainTest, RankWithoutSelectionCannotChain)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	auto * first = firstReborn();
	ASSERT_NE(first, nullptr);
	EXPECT_FALSE(newHorizonsElementalRebirth::captureDeathSource(*first, defenderSideHero));
	injure(first, first->getAvailableHealth());
	EXPECT_FALSE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
}

TEST_F(NewHorizonsRebirthChainTest, SideQuotaExcludesAnotherFirstGenerationAndRejectsClonePhoenixAndOrdinarySummons)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * first = firstReborn();
	ASSERT_NE(first, nullptr);
	CStack * otherSource = nullptr;
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit->unitSide() == BattleSide::DEFENDER && unit->unitSlot() == SlotID(1))
			otherSource = battle()->getStack(unit->unitId(), false);
	ASSERT_NE(otherSource, nullptr);
	injure(otherSource, otherSource->getAvailableHealth());
	CStack * otherFirst = nullptr;
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit->alive() && unit->getRebirthOriginalAggregateHP() > 0 && unit->unitId() != first->unitId())
			otherFirst = battle()->getStack(unit->unitId(), false);
	ASSERT_NE(otherFirst, nullptr);
	auto cloned = first->acquireState();
	cloned->cloned = true;
	EXPECT_FALSE(newHorizonsElementalRebirth::captureDeathSource(*cloned, defenderSideHero));
	auto phantom = first->acquireState();
	phantom->initializePhantomProfile(100, 2);
	EXPECT_FALSE(newHorizonsElementalRebirth::captureDeathSource(*phantom, defenderSideHero));
	for(const auto * id : {"core:airElemental", "core:phoenix"})
	{
		BattleHex position;
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			BattleHex candidate(index);
			if(candidate.isAvailable() && !battle()->battleGetUnitByPos(candidate, true))
			{
				position = candidate;
				break;
			}
		}
		battle::UnitInfo temporary;
		temporary.id = battle()->battleNextUnitId();
		temporary.type = creature(id);
		temporary.side = BattleSide::DEFENDER;
		temporary.position = position;
		temporary.count = 1;
		temporary.summoned = true;
		BattleUnitsChanged add;
		add.battleID = BattleID(0);
		add.changedStacks.emplace_back(temporary.id, UnitChanges::EOperation::ADD);
		temporary.save(add.changedStacks.back().data);
		gameHandler->sendAndApply(add);
		EXPECT_FALSE(newHorizonsElementalRebirth::captureDeathSource(
			*battle()->battleGetUnitByID(temporary.id), defenderSideHero));
	}
	injure(first, first->getAvailableHealth());
	ASSERT_TRUE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
	EXPECT_FALSE(newHorizonsElementalRebirth::captureDeathSource(*otherFirst, defenderSideHero, true));
	injure(otherFirst, otherFirst->getAvailableHealth());
	int survivingSummons = 0;
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit->alive() && unit->isSummoned())
			++survivingSummons;
	EXPECT_EQ(survivingSummons, 3) << "Only two ordinary summons and the single second-generation output remain";
}

TEST_F(NewHorizonsRebirthChainTest, SecondAppearanceAppliesPrimalBurst)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * first = firstReborn();
	ASSERT_NE(first, nullptr);
	bool moved = false;
	for(const auto & hex : first->getSurroundingHexes())
		if(hex.isAvailable() && !battle()->battleGetUnitByPos(hex, true))
		{
			battle()->moveUnit(enemy->unitId(), hex);
			moved = true;
			break;
		}
	ASSERT_TRUE(moved);
	const auto before = enemy->getAvailableHealth();
	const auto secondHP = first->getRebirthOriginalAggregateHP() / 4;
	injure(first, first->getAvailableHealth());
	EXPECT_EQ(before - enemy->getAvailableHealth(), newHorizonsElementalRebirth::primalBurstDamageBudget(secondHP));
}

TEST_F(NewHorizonsRebirthChainTest, ADDValidatesBeforeConsumingAndRejectsDuplicateUse)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * first = firstReborn();
	ASSERT_NE(first, nullptr);
	// A state UPDATE supplies the dead source without executing the reaction dispatcher.
	auto state = first->acquireState();
	int64_t lethal = state->getAvailableHealth();
	state->damage(lethal);
	BattleUnitsChanged death;
	death.battleID = BattleID(0);
	death.changedStacks.emplace_back(first->unitId(), UnitChanges::EOperation::UPDATE);
	death.changedStacks.back().data = state->save();
	gameHandler->sendAndApply(death);
	ASSERT_FALSE(first->alive());
	newHorizonsElementalRebirth::DeathSnapshot snapshot{first->unitId(), first->unitSide(),
		first->getPosition(), 0, *newHorizonsElementalRebirth::activeProfile(defenderSideHero),
		true, first->getRebirthOriginalAggregateHP()};
	EXPECT_FALSE(newHorizonsElementalRebirth::stillEligibleDeath(first, snapshot, true, false, true));
	EXPECT_FALSE(newHorizonsElementalRebirth::stillEligibleDeath(first, snapshot, true, true, false));
	auto add = candidate(first);
	const auto outputId = add.changedStacks.front().id;
	auto malformed = add;
	malformed.changedStacks.front().data["count"].Integer() += 1;
	EXPECT_THROW(gameHandler->sendAndApply(malformed), std::runtime_error);
	EXPECT_FALSE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
	EXPECT_EQ(battle()->battleGetUnitByID(outputId), nullptr);
	gameHandler->sendAndApply(add);
	EXPECT_TRUE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
	EXPECT_THROW(gameHandler->sendAndApply(add), std::runtime_error);
	BattleUnitsChanged rollback;
	rollback.battleID = BattleID(0);
	rollback.rebirthChainConsumption = *add.rebirthChainConsumption;
	rollback.rebirthChainConsumption->rollback = true;
	rollback.changedStacks.emplace_back(outputId, UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(rollback);
	EXPECT_FALSE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
}

TEST_F(NewHorizonsRebirthChainTest, SideUseAndPacketRoundTripWithLegacyGuards)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * first = firstReborn();
	ASSERT_NE(first, nullptr);
	// The candidate uses the actual empty corpse anchor, as the runtime does.
	// A generic UPDATE records death without dispatching Chain automatically.
	auto state = first->acquireState();
	int64_t lethal = state->getAvailableHealth();
	state->damage(lethal);
	BattleUnitsChanged death;
	death.battleID = BattleID(0);
	death.changedStacks.emplace_back(first->unitId(), UnitChanges::EOperation::UPDATE);
	death.changedStacks.back().data = state->save();
	gameHandler->sendAndApply(death);
	ASSERT_FALSE(first->alive());
	ASSERT_FALSE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
	auto packet = candidate(first);
	CMemorySerializer writer;
	writer.oser & packet;
	CMemorySerializer reader(writer.extractBuffer());
	BattleUnitsChanged restored;
	reader.iser & restored;
	ASSERT_TRUE(restored.rebirthChainConsumption);
	EXPECT_EQ(restored.rebirthChainConsumption->sourceUnitId, first->unitId());
	CMemorySerializer oldWriter;
	oldWriter.oser.version = ESerializationVersion::NEW_HORIZONS_LEARNING_MASTER_TEACHER;
	EXPECT_THROW(oldWriter.oser & packet, std::runtime_error);
	EXPECT_TRUE(oldWriter.extractBuffer().empty());
	battle()->setRebirthChainUsed(BattleSide::DEFENDER, true);
	SideInBattle side(gameState().get());
	side.rebirthChainUsed = true;
	CMemorySerializer sideWriter;
	sideWriter.oser & side;
	CMemorySerializer sideReader(sideWriter.extractBuffer());
	sideReader.iser.cb = gameState().get();
	SideInBattle restoredSide(gameState().get());
	sideReader.iser & restoredSide;
	EXPECT_TRUE(restoredSide.rebirthChainUsed);
	CMemorySerializer legacySide;
	legacySide.oser.version = ESerializationVersion::NEW_HORIZONS_LEARNING_MASTER_TEACHER;
	EXPECT_THROW(legacySide.oser & side, std::runtime_error);
	side.rebirthChainUsed = false;
	CMemorySerializer previousWriter;
	previousWriter.oser.version = ESerializationVersion::NEW_HORIZONS_LEARNING_MASTER_TEACHER;
	previousWriter.oser & side;
	CMemorySerializer previousReader(previousWriter.extractBuffer());
	previousReader.iser.version = ESerializationVersion::NEW_HORIZONS_LEARNING_MASTER_TEACHER;
	previousReader.iser.cb = gameState().get();
	restoredSide.rebirthChainUsed = true;
	previousReader.iser & restoredSide;
	EXPECT_FALSE(restoredSide.rebirthChainUsed);
}
