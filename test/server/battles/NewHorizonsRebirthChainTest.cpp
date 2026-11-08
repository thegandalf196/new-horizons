/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsRebirthChainFixture.h"
#include "../../../lib/serializer/CMemorySerializer.h"

class NewHorizonsRebirthChainTest : public NewHorizonsRebirthChainFixture {};

TEST_F(NewHorizonsRebirthChainTest, PrimalVigilanceStablePerkNeverReplacesEitherDestroyedStack)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto roster = battle()->battleGetAllUnits(false).size();
	const auto additions = server.unitAdditionRounds.size();
	const auto serial = battle()->getActivationSerial();
	std::vector<CStack *> victims;
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit->unitSide() == BattleSide::DEFENDER) victims.push_back(battle()->getStack(unit->unitId(), false));
	ASSERT_EQ(victims.size(), 2u);
	for(auto * victim : victims) injure(victim, victim->getAvailableHealth());
	EXPECT_EQ(battle()->battleGetAllUnits(false).size(), roster);
	EXPECT_EQ(server.unitAdditionRounds.size(), additions);
	EXPECT_EQ(battle()->getActivationSerial(), serial);
	EXPECT_FALSE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
}

TEST_F(NewHorizonsRebirthChainTest, RankWithoutVigilanceSelectionAlsoHasNoReplacement)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	const auto additions = server.unitAdditionRounds.size();
	injure(source, source->getAvailableHealth());
	EXPECT_FALSE(source->alive());
	EXPECT_EQ(server.unitAdditionRounds.size(), additions);
	EXPECT_FALSE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
}

TEST_F(NewHorizonsRebirthChainTest, IndependentElementalSummonReceivesVigilanceButDoesNotChainOnDeath)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	battle()->setElementalAttunementMask(BattleSide::DEFENDER, 2);
	BattleHex position;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		if(BattleHex(index).isAvailable() && !battle()->battleGetUnitByPos(BattleHex(index), true))
		{ position = BattleHex(index); break; }
	ASSERT_TRUE(position.isValid());
	battle::UnitInfo info;
	info.id = battle()->battleNextUnitId(); info.type = creature("core:fireElemental");
	info.side = BattleSide::DEFENDER; info.position = position; info.count = 2; info.summoned = true;
	BattleUnitsChanged add;
	add.battleID = BattleID(0);
	add.changedStacks.emplace_back(info.id, UnitChanges::EOperation::ADD);
	info.save(add.changedStacks.back().data);
	ASSERT_NO_THROW(gameHandler->sendAndApply(add));
	auto * summoned = battle()->getStack(info.id, false);
	ASSERT_NE(summoned, nullptr);
	EXPECT_TRUE(summoned->isSummoned());
	EXPECT_TRUE(summoned->elementalAttunementPassives().attuned);
	EXPECT_EQ(summoned->counterAttacks.total(), 2);
	const auto additions = server.unitAdditionRounds.size();
	injure(summoned, summoned->getAvailableHealth());
	EXPECT_FALSE(summoned->alive());
	EXPECT_EQ(server.unitAdditionRounds.size(), additions);
	EXPECT_FALSE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
}

TEST_F(NewHorizonsRebirthChainTest, DestroyedStackDoesNotEmitPrimalBurstDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	BattleHex adjacent;
	for(const auto hex : source->getSurroundingHexes())
		if(hex.isAvailable() && !battle()->battleGetUnitByPos(hex, true)) { adjacent = hex; break; }
	ASSERT_TRUE(adjacent.isValid());
	battle()->moveUnit(enemy->unitId(), adjacent);
	const auto health = enemy->getAvailableHealth();
	injure(source, source->getAvailableHealth());
	EXPECT_EQ(enemy->getAvailableHealth(), health);
	EXPECT_FALSE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
}

TEST_F(NewHorizonsRebirthChainTest, RetiredConsumptionRequestRejectsBeforeResourcesOrRosterMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	injure(source, source->getAvailableHealth());
	const auto before = battle()->battleGetAllUnits(false).size();
	const auto resources = battle()->getCommandResources(BattleSide::DEFENDER);
	const auto allowances = battle()->getHeroActionAllowances(BattleSide::DEFENDER);
	auto packet = retiredCandidate();
	EXPECT_THROW(gameHandler->sendAndApply(packet), std::runtime_error);
	EXPECT_EQ(battle()->battleGetAllUnits(false).size(), before);
	EXPECT_EQ(battle()->getCommandResources(BattleSide::DEFENDER), resources);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::DEFENDER), allowances);
	EXPECT_EQ(battle()->battleGetUnitByID(packet.changedStacks.front().id), nullptr);
	EXPECT_FALSE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
}

TEST_F(NewHorizonsRebirthChainTest, ReservedReceiptCodecCannotAuthorizeAReplacementInCurrentBattle)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	injure(source, source->getAvailableHealth());
	auto packet = retiredCandidate();
	CMemorySerializer current;
	ASSERT_NO_THROW(current.oser & packet);
	BattleUnitsChanged decoded;
	ASSERT_NO_THROW(current.iser & decoded);
	ASSERT_TRUE(decoded.rebirthChainConsumption);
	EXPECT_EQ(decoded.rebirthChainConsumption->sourceUnitId, source->unitId());
	const auto before = battle()->battleGetAllUnits(false).size();
	EXPECT_THROW(gameHandler->sendAndApply(decoded), std::runtime_error);
	EXPECT_EQ(battle()->battleGetAllUnits(false).size(), before);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_LEARNING_MASTER_TEACHER;
	EXPECT_THROW(old.oser & packet, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	SideInBattle side(gameState().get());
	side.rebirthChainUsed = true; // Reserved leaf codec only, never admitted battle state.
	CMemorySerializer leaf;
	ASSERT_NO_THROW(leaf.oser & side);
	leaf.iser.cb = gameState().get();
	SideInBattle restored(gameState().get());
	ASSERT_NO_THROW(leaf.iser & restored);
	EXPECT_TRUE(restored.rebirthChainUsed);
}
