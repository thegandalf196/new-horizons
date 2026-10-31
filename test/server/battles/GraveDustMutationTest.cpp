/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../server/CGameHandler.h"
#include "../../../lib/battle/GraveDustEffectChanges.h"
#include "../../../lib/battle/NewHorizonsGraveDust.h"
#include "../../../lib/battle/HealingReceipt.h"
#include "../../../lib/battle/PhysicalAffliction.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/gameState/GameStatePackVisitor.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include <stdexcept>
namespace
{
class GraveDustMutationTest : public BattleTestFixture, public ::testing::WithParamInterface<int>
{
protected:
	CStack * first = nullptr;
	CStack * second = nullptr;
	void prepare()
	{
		startGame();
		startBattle();
		first = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
		second = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 2);
		ASSERT_NE(first, nullptr);
		ASSERT_NE(second, nullptr);
		beginCombat();
	}
	void apply(SetStackEffect & effects)
	{
		effects.battleID = battle()->getBattleID();
		if(GetParam() == 2) gameHandler->sendAndApply(effects);
		else if(GetParam() == 1)
		{
			BattleStatePackVisitor visitor(*battle());
			visitor.visitSetStackEffect(effects);
		}
		else
		{
			for(const auto & [id, records] : effects.toRemove) battle()->removeUnitBonus(id, records);
			for(const auto & [id, records] : effects.toUpdate) battle()->updateUnitBonus(id, records);
			for(const auto & [id, records] : effects.toAdd) battle()->addUnitBonus(id, records);
		}
	}
	std::vector<Bonus> group(const battle::Unit & unit)
	{
		std::vector<Bonus> result;
		const auto actual = unit.getUnstackedBonuses(CSelector([](const Bonus * bonus)
		{
			return bonus && newHorizonsGraveDust::isReservedSource(*bonus);
		}));
		for(const auto & bonus : *actual) result.push_back(*bonus);
		return result;
	}
	void expectDust(const battle::Unit & unit, int32_t reduction)
	{
		EXPECT_EQ(battle::healingReceivedReduction(unit), reduction);
		const auto actual = group(unit);
		EXPECT_EQ(actual.size(), reduction ? 2u : 0u);
		if(reduction) EXPECT_NO_THROW(newHorizonsGraveDust::validateSourceGroup(actual));
	}
};
TEST_P(GraveDustMutationTest, CompleteAddRefreshAndRemovalUseActualOperationSemantics)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	SetStackEffect added;
	added.toAdd.emplace_back(first->unitId(), dust);
	ASSERT_NO_THROW(apply(added));
	expectDust(*first, 5000);
	const auto initial = physicalAfflictions::enumerate(*first);
	ASSERT_EQ(initial.size(), 1u);
	ASSERT_GT(initial.front().applicationOrder, 0u);
	SetStackEffect refreshed;
	refreshed.toUpdate.emplace_back(first->unitId(), dust);
	ASSERT_NO_THROW(apply(refreshed));
	expectDust(*first, 5000);
	EXPECT_EQ(physicalAfflictions::enumerate(*first).front().applicationOrder, initial.front().applicationOrder);
	SetStackEffect removed;
	removed.toRemove.emplace_back(first->unitId(), group(*first));
	ASSERT_NO_THROW(apply(removed));
	expectDust(*first, 0);
}
TEST_P(GraveDustMutationTest, PartialAddDuplicateCompanionAndForeignPlayerFailBeforeMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	for(int failure = 0; failure < 4; ++failure)
	{
		auto records = dust;
		if(failure == 0) records.pop_back();
		if(failure == 1) records.erase(records.begin());
		if(failure == 2) records.push_back(dust.back());
		if(failure == 3) for(auto & record : records) record.spellCasterOwner = PlayerColor(7);
		SetStackEffect effects;
		effects.toAdd.emplace_back(first->unitId(), records);
		const auto before = first->acquireState()->save();
		EXPECT_THROW(apply(effects), std::invalid_argument);
		EXPECT_EQ(first->acquireState()->save(), before);
		expectDust(*first, 0);
	}
}
TEST_P(GraveDustMutationTest, PartialRemoveAndStrengthOrCasterUpdateCannotLeaveOrSilentlyChangeGroup)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	battle()->addUnitBonus(first->unitId(), dust);
	const auto original = physicalAfflictions::enumerate(*first).front().applicationOrder;
	for(int failure = 0; failure < 4; ++failure)
	{
		SetStackEffect effects;
		if(failure == 0) effects.toRemove.emplace_back(first->unitId(), std::vector<Bonus>{dust.front()});
		if(failure == 1) effects.toRemove.emplace_back(first->unitId(), std::vector<Bonus>{dust.back()});
		if(failure == 2) effects.toUpdate.emplace_back(first->unitId(), newHorizonsGraveDust::records(7500, PlayerColor(0)));
		if(failure == 3) effects.toUpdate.emplace_back(first->unitId(), newHorizonsGraveDust::records(5000, PlayerColor(1)));
		EXPECT_THROW(apply(effects), std::invalid_argument);
		expectDust(*first, 5000);
		EXPECT_EQ(physicalAfflictions::enumerate(*first).front().applicationOrder, original);
	}
}
TEST_P(GraveDustMutationTest, StrongerCompleteReplacementAndDeadCleanupAreAccepted)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	battle()->addUnitBonus(first->unitId(), newHorizonsGraveDust::records(5000, PlayerColor(0)));
	SetStackEffect replacement;
	replacement.toRemove.emplace_back(first->unitId(), group(*first));
	replacement.toAdd.emplace_back(first->unitId(), newHorizonsGraveDust::records(7500, PlayerColor(1)));
	ASSERT_NO_THROW(apply(replacement));
	expectDust(*first, 7500);
	int64_t lethal = first->getAvailableHealth();
	first->damage(lethal);
	ASSERT_FALSE(first->alive());
	SetStackEffect cleanup;
	cleanup.toRemove.emplace_back(first->unitId(), group(*first));
	ASSERT_NO_THROW(apply(cleanup));
	expectDust(*first, 0);
	SetStackEffect reapplied;
	reapplied.toAdd.emplace_back(first->unitId(), newHorizonsGraveDust::records(5000, PlayerColor(0)));
	EXPECT_THROW(apply(reapplied), std::invalid_argument);
	expectDust(*first, 0);
}
TEST_P(GraveDustMutationTest, PacketPreflightChecksLaterTargetAndSplitOperationsBeforeEarlierMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	SetStackEffect multi;
	multi.toAdd.emplace_back(first->unitId(), dust);
	multi.toAdd.emplace_back(second->unitId(), std::vector<Bonus>{dust.front()});
	EXPECT_THROW(newHorizonsGraveDust::validateEffectChanges(*battle(), multi), std::invalid_argument);
	if(GetParam() != 0) EXPECT_THROW(apply(multi), std::invalid_argument);
	expectDust(*first, 0);
	expectDust(*second, 0);
	SetStackEffect split;
	split.toAdd.emplace_back(first->unitId(), std::vector<Bonus>{dust.front()});
	split.toAdd.emplace_back(first->unitId(), std::vector<Bonus>{dust.back()});
	EXPECT_THROW(newHorizonsGraveDust::validateEffectChanges(*battle(), split), std::invalid_argument);
	if(GetParam() != 0) EXPECT_THROW(apply(split), std::invalid_argument);
	expectDust(*first, 0);
}
TEST_P(GraveDustMutationTest, MalformedReservedRecordCannotMutateAnEarlierValidTarget)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	auto forged = dust;
	forged.front().source = BonusSource::HERO_SPECIAL;
	SetStackEffect effects;
	effects.toAdd.emplace_back(first->unitId(), dust);
	effects.toAdd.emplace_back(second->unitId(), forged);
	EXPECT_THROW(newHorizonsGraveDust::validateEffectChanges(*battle(), effects), std::invalid_argument);
	if(GetParam() != 0) EXPECT_THROW(apply(effects), std::invalid_argument);
	expectDust(*first, 0);
	expectDust(*second, 0);
}
TEST_P(GraveDustMutationTest, TimeStopRetainsNativeRemovalNoOpAndRefusesIncomingDust)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	battle()->addUnitBonus(first->unitId(), dust);
	const Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	first->addNewBonus(std::make_shared<Bonus>(stopped));
	ASSERT_TRUE(first->isTimeStopped());
	SetStackEffect removed;
	removed.toRemove.emplace_back(first->unitId(), group(*first));
	ASSERT_NO_THROW(apply(removed));
	expectDust(*first, 5000);
	SetStackEffect updated;
	updated.toUpdate.emplace_back(first->unitId(), dust);
	EXPECT_THROW(apply(updated), std::invalid_argument);
	SetStackEffect added;
	added.toAdd.emplace_back(first->unitId(), dust);
	EXPECT_THROW(apply(added), std::invalid_argument);
	expectDust(*first, 5000);
}
INSTANTIATE_TEST_SUITE_P(NativeReplicaAuthority, GraveDustMutationTest, ::testing::Values(0, 1, 2));
}
