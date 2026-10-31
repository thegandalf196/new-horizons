/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../server/CGameHandler.h"
#include "../../../lib/battle/GraveDustBodyCleanup.h"
#include "../../../lib/battle/NewHorizonsGraveDust.h"
#include "../../../lib/battle/HealingReceipt.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
namespace
{
class GraveDustBodyCleanupTest : public BattleTestFixture, public ::testing::WithParamInterface<bool>
{
protected:
	CStack * source = nullptr;
	CStack * target = nullptr;
	void prepare(bool dust = true, bool rebirth = false, bool conditional = false)
	{
		startGame();
		startBattle();
		source = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 2);
		ASSERT_NE(source, nullptr);
		ASSERT_NE(target, nullptr);
		beginCombat();
		if(dust) battle()->addUnitBonus(target->unitId(), newHorizonsGraveDust::records(10'000, PlayerColor(0)));
		if(rebirth)
			for(const auto [type, value] : {std::pair{BonusType::REBIRTH, 100}, std::pair{BonusType::CASTS, 1}})
			{
				auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, type, BonusSource::OTHER, value, BonusSourceID());
				if(conditional) bonus->limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::HEALING_RECEIVED_REDUCTION_BASIS_POINTS);
				target->addNewBonus(std::move(bonus));
			}
	}
	BattleStackAttacked hit(int64_t damage)
	{
		BattleStackAttacked result;
		result.attackerID = source->unitId();
		result.stackAttacked = target->unitId();
		result.damageAmount = damage;
		CStack::prepareAttacked(result, gameHandler->getRandomGenerator(), target->acquireState());
		return result;
	}
	void publish(const std::vector<BattleStackAttacked> & hits)
	{
		if(GetParam())
		{
			BattleAttack pack;
			pack.battleID = battle()->getBattleID();
			pack.stackAttacking = source->unitId();
			pack.attackerChanges.battleID = pack.battleID;
			UnitChanges change(source->unitId(), UnitChanges::EOperation::UPDATE);
			change.data = source->acquireState()->save();
			pack.attackerChanges.changedStacks.push_back(std::move(change));
			pack.bsa = hits;
			gameHandler->sendAndApply(pack);
		}
		else
		{
			StacksInjured pack;
			pack.battleID = battle()->getBattleID();
			pack.stacks = hits;
			gameHandler->sendAndApply(pack);
		}
	}
};
TEST_P(GraveDustBodyCleanupTest, DeathDispersesDustBeforeOrdinaryResurrection)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto lethal = hit(target->getAvailableHealth());
	ASSERT_TRUE(lethal.killed());
	ASSERT_FALSE(lethal.willRebirth());
	ASSERT_NO_THROW(publish({lethal}));
	ASSERT_FALSE(target->alive());
	EXPECT_EQ(battle::healingReceivedReduction(*target), 0);
	auto restored = target->acquireState();
	int64_t request = 1000;
	const auto receipt = restored->heal(request, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	EXPECT_EQ(receipt.resurrectedCount, 2);
	EXPECT_EQ(restored->getCount(), 2);
	EXPECT_EQ(request, restored->getTotalHealth());
}
TEST_P(GraveDustBodyCleanupTest, NativeRebirthKeepsRawRecreationAndDispersesOldDust)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, true));
	const auto lethal = hit(target->getAvailableHealth());
	ASSERT_TRUE(lethal.killed());
	ASSERT_TRUE(lethal.willRebirth());
	ASSERT_EQ(battle::healingReceivedReduction(*target), 10'000); // Preparation did not mutate its source.
	ASSERT_NO_THROW(publish({lethal}));
	EXPECT_TRUE(target->alive());
	EXPECT_EQ(target->getCount(), 2);
	EXPECT_EQ(target->getAvailableHealth(), target->getTotalHealth());
	EXPECT_EQ(battle::healingReceivedReduction(*target), 0);
}
TEST_P(GraveDustBodyCleanupTest, ConditionalRebirthAndCastsArePreparedBeforeDustRemoval)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, true, true));
	ASSERT_TRUE(target->canCast());
	const auto lethal = hit(target->getAvailableHealth());
	ASSERT_TRUE(lethal.willRebirth()); // Both raw conditional records were active at the native preparation stage.
	ASSERT_NO_THROW(publish({lethal}));
	EXPECT_TRUE(target->alive());
	EXPECT_EQ(target->getCount(), 2);
	EXPECT_EQ(battle::healingReceivedReduction(*target), 0);
	EXPECT_FALSE(target->hasBonusOfType(BonusType::REBIRTH));
	EXPECT_FALSE(target->canCast());
}
TEST_P(GraveDustBodyCleanupTest, SurvivingCasualtiesRetainDustAndBlockEligibleRestoration)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto partial = hit(target->getMaxHealth());
	ASSERT_FALSE(partial.killed());
	ASSERT_FALSE(partial.willRebirth());
	ASSERT_NO_THROW(publish({partial}));
	ASSERT_TRUE(target->alive());
	ASSERT_EQ(target->getCount(), 1);
	EXPECT_EQ(battle::healingReceivedReduction(*target), 10'000);
	auto restored = target->acquireState();
	int64_t request = 1000;
	EXPECT_EQ(restored->heal(request, EHealLevel::RESURRECT, EHealPower::PERMANENT).resurrectedCount, 0);
	EXPECT_EQ(request, 0);
	EXPECT_EQ(restored->getCount(), 1);
}
TEST_P(GraveDustBodyCleanupTest, InvalidLaterGroupIsRejectedBeforeAnyEarlierRemoval)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto validHit = hit(target->getAvailableHealth());
	BattleStackAttacked sourceHit;
	sourceHit.attackerID = target->unitId();
	sourceHit.stackAttacked = source->unitId();
	sourceHit.damageAmount = source->getAvailableHealth();
	CStack::prepareAttacked(sourceHit, gameHandler->getRandomGenerator(), source->acquireState());
	ASSERT_TRUE(sourceHit.killed());
	source->addNewBonus(std::make_shared<Bonus>(newHorizonsGraveDust::records(5000, PlayerColor(1)).back()));
	EXPECT_THROW(publish({validHit, sourceHit}), std::invalid_argument);
	EXPECT_TRUE(target->alive());
	EXPECT_TRUE(source->alive());
	EXPECT_EQ(battle::healingReceivedReduction(*target), 10'000);
}
INSTANTIATE_TEST_SUITE_P(DirectAndIndirectNativePackets, GraveDustBodyCleanupTest, ::testing::Bool());
}
