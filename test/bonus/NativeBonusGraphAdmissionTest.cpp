/*
 * NativeBonusGraphAdmissionTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../lib/bonuses/NativeBonusGraphAdmission.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include "../../lib/battle/PreparedBattleWorldCommit.h"
#include "../../lib/battle/BattleEffectExchange.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <type_traits>

TEST(NativeBonusGraphAdmissionTest, QueriesAndOrdinaryNestedOwnersRemainLegal)
{
	CBonusSystemNode node(BonusNodeType::CREATURE);
	node.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::STACKS_SPEED, BonusSource::OTHER, 7, BonusSourceID()));
	{
		NativeBonusGraphMutation first;
		NativeBonusGraphMutation nested;
		EXPECT_EQ(node.valOfBonuses(BonusType::STACKS_SPEED), 7);
	}
	{
		NativeBonusGraphPreparation preparation;
		NativeBonusGraphAccess query;
		EXPECT_EQ(node.valOfBonuses(BonusType::STACKS_SPEED), 7);
		EXPECT_THROW(NativeBonusGraphMutation{}, std::logic_error);
		EXPECT_THROW(NativeBonusGraphPreparation{}, std::logic_error);
		EXPECT_EQ(node.valOfBonuses(BonusType::STACKS_SPEED), 7);
	}
	EXPECT_NO_THROW(NativeBonusGraphMutation{});
}

TEST(NativeBonusGraphAdmissionTest, NativeVisitorCannotPromoteItselfIntoPreparation)
{
	{
		NativeBonusGraphMutation ordinary;
		EXPECT_THROW(NativeBonusGraphPreparation{}, std::logic_error);
		EXPECT_NO_THROW(NativeBonusGraphMutation{});
	}
	EXPECT_NO_THROW(NativeBonusGraphPreparation{});
}

TEST(NativeBonusGraphAdmissionTest, ThrowingPreparationReleasesPhaseWithoutMovingThreadOwnership)
{
	static_assert(!std::is_move_constructible_v<NativeBonusGraphPreparation>);
	static_assert(!std::is_copy_constructible_v<NativeBonusGraphMutation>);
	static_assert(std::is_nothrow_destructible_v<NativeBonusGraphPreparation>);
	EXPECT_THROW(([]
	{
		NativeBonusGraphPreparation preparation;
		throw std::runtime_error("preparation failed");
	}()), std::runtime_error);
	EXPECT_NO_THROW(NativeBonusGraphMutation{});
	EXPECT_NO_THROW(NativeBonusGraphPreparation{});
}

namespace
{
class NativePreparationOwnerTest : public HeroCommandFixture
{
protected:
	CStack * unit = nullptr;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		startGame();
		startBattle();
		unit = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 4);
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::STACKS_SPEED, BonusSource::OTHER, 7, BonusSourceID()));
		gameHandler->gs->replayLog.configure(false, 2);
		gameHandler->gs->replayLog.beginDay(gameHandler->gs->day, {std::byte{0x11}});
	}
	BattleUnitsChanged packet()
	{
		BattleUnitsChanged result;
		result.battleID = battle()->getBattleID();
		result.changedStacks.emplace_back(unit->unitId(), BattleChanges::EOperation::UPDATE);
		result.changedStacks.back().data = unit->save();
		result.changedStacks.back().data["state"]["defending"].Bool() = true;
		return result;
	}
	std::vector<std::byte> replay()
	{
		CMemorySerializer serializer;
		serializer.oser & gameHandler->gs->replayLog;
		return serializer.extractBuffer();
	}
};
}

TEST_F(NativePreparationOwnerTest, RealWorldOwnerRejectsQueryReenteredApplyBeforeReplayAndValues)
{
	auto change = packet();
	const auto before = unit->save();
	const auto beforeReplay = replay();
	{
		battle::PreparedBattleWorldCommit owner(gameHandler->gs,
			std::unique_lock<std::shared_mutex>(CGameState::mutex), change);
		const auto generation = PreparedBonusGraphStorage::currentGeneration();
		bool entered = false;
		CSelector query([&](const Bonus *)
		{
			entered = true;
			gameHandler->gs->apply(change); // actual mutation owner, not a mock callback permit
			return true;
		});
		EXPECT_THROW(unit->getAllBonuses(query), std::logic_error);
		EXPECT_TRUE(entered);
		EXPECT_EQ(unit->save(), before);
		EXPECT_EQ(replay(), beforeReplay);
		EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	}
	EXPECT_NO_THROW(gameHandler->gs->apply(change));
	EXPECT_TRUE(unit->defending);
}

TEST_F(NativePreparationOwnerTest, NativeEffectExchangeRefusesBeforeShapeValidationOrMutation)
{
	const auto before = unit->save();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	{
		NativeBonusGraphPreparation preparation;
		battle::BattleEffectExchange malformed;
		EXPECT_THROW(battle()->exchangeBattleEffects(malformed), std::logic_error);
		EXPECT_EQ(unit->save(), before);
		EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	}
	EXPECT_THROW(battle()->exchangeBattleEffects(battle::BattleEffectExchange{}), std::runtime_error);
}

TEST_F(NativePreparationOwnerTest, FailedRealWorldCaptureDoesNotStrandAdmission)
{
	auto change = packet();
	change.changedStacks.emplace_back(999999, BattleChanges::EOperation::UPDATE);
	const auto before = unit->save();
	const auto beforeReplay = replay();
	EXPECT_THROW((battle::PreparedBattleWorldCommit(gameHandler->gs,
		std::unique_lock<std::shared_mutex>(CGameState::mutex), change)), std::runtime_error);
	EXPECT_EQ(unit->save(), before);
	EXPECT_EQ(replay(), beforeReplay);
	auto recovery = packet();
	EXPECT_NO_THROW(gameHandler->gs->apply(recovery));
	EXPECT_TRUE(unit->defending);
}

TEST_F(NativePreparationOwnerTest, ActualScopeTransfersWithoutReopeningOrdinaryMutation)
{
	auto change = packet();
	std::unique_lock state(CGameState::mutex);
	auto preparation = std::make_unique<NativeBonusGraphPreparation>();
	ASSERT_TRUE(preparation->activeOnThisThread());
	auto owner = std::make_unique<battle::PreparedBattleWorldCommit>(gameHandler->gs,
		std::move(state), change, preparation);
	EXPECT_FALSE(preparation);
	EXPECT_THROW(NativeBonusGraphMutation{}, std::logic_error);
	EXPECT_THROW(NativeBonusGraphPreparation::checkEntry(), std::logic_error);
	owner.reset();
	EXPECT_NO_THROW(NativeBonusGraphMutation{});
	EXPECT_NO_THROW(NativeBonusGraphPreparation::checkEntry());
}

TEST_F(NativePreparationOwnerTest, NullTransferredScopeRefusesBeforeCaptureAndKeepsSourceNull)
{
	std::unique_ptr<NativeBonusGraphPreparation> absent;
	const auto before = unit->save();
	const auto beforeReplay = replay();
	EXPECT_THROW((battle::PreparedBattleWorldCommit(gameHandler->gs,
		std::unique_lock<std::shared_mutex>(CGameState::mutex), packet(), absent)), std::invalid_argument);
	EXPECT_FALSE(absent);
	EXPECT_EQ(unit->save(), before);
	EXPECT_EQ(replay(), beforeReplay);
	EXPECT_NO_THROW(NativeBonusGraphMutation{});
}

TEST_F(NativePreparationOwnerTest, FailureAfterScopeTransferReleasesBothAdmissionAndState)
{
	auto change = packet();
	change.changedStacks.emplace_back(999999, BattleChanges::EOperation::UPDATE);
	std::unique_lock state(CGameState::mutex);
	auto preparation = std::make_unique<NativeBonusGraphPreparation>();
	EXPECT_THROW((battle::PreparedBattleWorldCommit(gameHandler->gs,
		std::move(state), change, preparation)), std::runtime_error);
	EXPECT_FALSE(preparation);
	EXPECT_NO_THROW(NativeBonusGraphMutation{});
	std::unique_lock released(CGameState::mutex, std::try_to_lock);
	EXPECT_TRUE(released.owns_lock());
}
