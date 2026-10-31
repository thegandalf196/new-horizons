/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../server/CGameHandler.h"
#include "../../../lib/battle/GraveDustEffectChanges.h"
#include "../../../lib/battle/GraveDustNativePreflight.h"
#include "../../../lib/battle/NewHorizonsParalyzingGaze.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/battle/NewHorizonsGraveDust.h"
#include "../../../lib/battle/HealingReceipt.h"
#include "../../../lib/battle/PhysicalAffliction.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/gameState/GameStatePackVisitor.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include <tuple>
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

TEST_P(GraveDustMutationTest, RemovingTimeStopBeforePartialDustFailsBeforeEitherLiveRecordChanges)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	battle()->addUnitBonus(first->unitId(), dust);
	const Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	first->addNewBonus(std::make_shared<Bonus>(stopped));
	for(const auto & partial : dust)
	{
		SetStackEffect removed;
		removed.toRemove.emplace_back(first->unitId(), std::vector<Bonus>{stopped, partial});
		const auto before = first->acquireState()->save();
		EXPECT_THROW(apply(removed), std::invalid_argument);
		EXPECT_EQ(first->acquireState()->save(), before);
		EXPECT_TRUE(first->isTimeStopped());
		expectDust(*first, 5000);
	}
}
TEST_P(GraveDustMutationTest, PartialDustBeforeTimeStopIsIgnoredAndCompleteDustAfterTimeStopIsAccepted)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	battle()->addUnitBonus(first->unitId(), dust);
	const Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	first->addNewBonus(std::make_shared<Bonus>(stopped));
	SetStackEffect reversed;
	reversed.toRemove.emplace_back(first->unitId(), std::vector<Bonus>{dust.front(), stopped});
	ASSERT_NO_THROW(apply(reversed));
	EXPECT_FALSE(first->isTimeStopped());
	expectDust(*first, 5000);
	first->addNewBonus(std::make_shared<Bonus>(stopped));
	SetStackEffect complete;
	complete.toRemove.emplace_back(first->unitId(), std::vector<Bonus>{stopped, dust.front(), dust.back()});
	ASSERT_NO_THROW(apply(complete));
	EXPECT_FALSE(first->isTimeStopped());
	expectDust(*first, 0);
}
TEST_P(GraveDustMutationTest, OtherRemainingOrNonmatchingTimeStopKeepsPartialDustProtected)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	battle()->addUnitBonus(first->unitId(), dust);
	const Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	Bonus other = stopped; other.val = 2;
	first->addNewBonus(std::make_shared<Bonus>(stopped));
	first->addNewBonus(std::make_shared<Bonus>(other));
	SetStackEffect remaining;
	remaining.toRemove.emplace_back(first->unitId(), std::vector<Bonus>{stopped, dust.front()});
	ASSERT_NO_THROW(apply(remaining));
	EXPECT_TRUE(first->isTimeStopped());
	expectDust(*first, 5000);
	SetStackEffect nonmatching;
	nonmatching.toRemove.emplace_back(first->unitId(), std::vector<Bonus>{stopped, dust.back()});
	ASSERT_NO_THROW(apply(nonmatching));
	EXPECT_TRUE(first->isTimeStopped());
	expectDust(*first, 5000);
	SetStackEffect last;
	last.toRemove.emplace_back(first->unitId(), std::vector<Bonus>{other, dust.front()});
	EXPECT_THROW(apply(last), std::invalid_argument);
	EXPECT_TRUE(first->isTimeStopped());
	expectDust(*first, 5000);
}
TEST_P(GraveDustMutationTest, SeparateRemovalCallsArePreflightedInPacketOrderBeforeTimeStopMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	battle()->addUnitBonus(first->unitId(), dust);
	const Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	first->addNewBonus(std::make_shared<Bonus>(stopped));
	SetStackEffect split;
	split.toRemove.emplace_back(first->unitId(), std::vector<Bonus>{stopped});
	split.toRemove.emplace_back(first->unitId(), std::vector<Bonus>{dust.front()});
	EXPECT_THROW(newHorizonsGraveDust::validateEffectChanges(*battle(), split), std::invalid_argument);
	if(GetParam() != 0) EXPECT_THROW(apply(split), std::invalid_argument);
	EXPECT_TRUE(first->isTimeStopped());
	expectDust(*first, 5000);
	std::reverse(split.toRemove.begin(), split.toRemove.end());
	ASSERT_NO_THROW(newHorizonsGraveDust::validateEffectChanges(*battle(), split));
	ASSERT_NO_THROW(apply(split));
	EXPECT_FALSE(first->isTimeStopped());
	expectDust(*first, 5000);
}

TEST_P(GraveDustMutationTest, TimeStopBetweenNewDustCompanionsRejectsBothAddAndUpdateBeforeMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	const Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	for(const bool add : {false, true})
		for(const bool reversed : {false, true})
		{
			SetStackEffect effects;
			auto & changes = add ? effects.toAdd : effects.toUpdate;
			changes.emplace_back(first->unitId(), std::vector<Bonus>{dust.at(reversed ? 1 : 0), stopped, dust.at(reversed ? 0 : 1)});
			const auto before = first->acquireState()->save();
			EXPECT_THROW(apply(effects), std::invalid_argument);
			EXPECT_EQ(first->acquireState()->save(), before);
			EXPECT_FALSE(first->isTimeStopped());
			expectDust(*first, 0);
		}
}
TEST_P(GraveDustMutationTest, ClockBeforeWholeNewDustIgnoresItAndClockAfterWholePairKeepsIt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	const Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	for(const bool add : {false, true})
	{
		SetStackEffect clockFirst;
		auto & firstChanges = add ? clockFirst.toAdd : clockFirst.toUpdate;
		firstChanges.emplace_back(first->unitId(), std::vector<Bonus>{stopped, dust.front(), dust.back()});
		ASSERT_NO_THROW(apply(clockFirst));
		EXPECT_TRUE(first->isTimeStopped());
		expectDust(*first, 0);
		battle()->removeUnitBonus(first->unitId(), {stopped});
		SetStackEffect clockLast;
		auto & lastChanges = add ? clockLast.toAdd : clockLast.toUpdate;
		lastChanges.emplace_back(first->unitId(), std::vector<Bonus>{dust.front(), dust.back(), stopped});
		ASSERT_NO_THROW(apply(clockLast));
		EXPECT_TRUE(first->isTimeStopped());
		expectDust(*first, 5000);
		battle()->removeUnitBonus(first->unitId(), {stopped});
		battle()->removeUnitBonus(first->unitId(), group(*first));
	}
}
TEST_P(GraveDustMutationTest, EarlierClockRemovalMakesFollowingWholeDustAddOrUpdateAdmissible)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	const Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	for(const bool add : {false, true})
	{
		first->addNewBonus(std::make_shared<Bonus>(stopped));
		SetStackEffect effects;
		effects.toRemove.emplace_back(first->unitId(), std::vector<Bonus>{stopped});
		auto & changes = add ? effects.toAdd : effects.toUpdate;
		changes.emplace_back(first->unitId(), dust);
		ASSERT_NO_THROW(newHorizonsGraveDust::validateEffectChanges(*battle(), effects));
		ASSERT_NO_THROW(apply(effects));
		EXPECT_FALSE(first->isTimeStopped());
		expectDust(*first, 5000);
		battle()->removeUnitBonus(first->unitId(), group(*first));
	}
}

TEST_P(GraveDustMutationTest, InheritedClockSurvivesMatchingRemovalEvenWhenItsPointerAlsoOwnsALocalExport)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	battle()->addUnitBonus(first->unitId(), dust);
	const Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	for(const bool localAlias : {false, true})
	{
		CBonusSystemNode parent(BonusNodeType::HERO);
		auto shared = std::make_shared<Bonus>(stopped); parent.addNewBonus(shared);
		first->attachTo(parent);
		if(localAlias) first->addNewBonus(shared);
		ASSERT_TRUE(first->isTimeStopped());
		SetStackEffect effects;
		effects.toRemove.emplace_back(first->unitId(), std::vector<Bonus>{stopped, dust.front()});
		ASSERT_NO_THROW(apply(effects));
		EXPECT_TRUE(first->isTimeStopped()); EXPECT_TRUE(parent.hasBonusOfType(BonusType::TIME_STOP));
		expectDust(*first, 5000);
		first->detachFrom(parent);
		EXPECT_FALSE(first->isTimeStopped());
		expectDust(*first, 5000);
	}
}
TEST_P(GraveDustMutationTest, RemovalMatchesRawClockExportBeforeItsUpdaterMaterializesDifferentStrength)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	battle()->addUnitBonus(first->unitId(), dust);
	Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	stopped.updater = std::make_shared<TimesStackSizeUpdater>(0, 1000, 1, 1);
	first->addNewBonus(std::make_shared<Bonus>(stopped));
	const auto effective = first->getBonuses(Selector::type()(BonusType::TIME_STOP));
	ASSERT_EQ(effective->size(), 1u); ASSERT_EQ(effective->front()->val, 2);
	SetStackEffect partial;
	partial.toRemove.emplace_back(first->unitId(), std::vector<Bonus>{stopped, dust.front()});
	EXPECT_THROW(apply(partial), std::invalid_argument);
	EXPECT_TRUE(first->isTimeStopped());
	expectDust(*first, 5000);
	SetStackEffect complete;
	complete.toRemove.emplace_back(first->unitId(), std::vector<Bonus>{stopped, dust.front(), dust.back()});
	ASSERT_NO_THROW(apply(complete));
	EXPECT_FALSE(first->isTimeStopped());
	expectDust(*first, 0);
}

TEST_P(GraveDustMutationTest, GuardianEntryCanActivateAConditionalClockBetweenDustCompanions)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	stopped.limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::GUARDIAN_SPIRIT);
	first->addNewBonus(std::make_shared<Bonus>(stopped));
	ASSERT_FALSE(first->isTimeStopped());
	Bonus guardian(BonusDuration::N_TURNS, BonusType::GUARDIAN_SPIRIT, BonusSource::OTHER, 5, BonusSourceID());
	guardian.turnsRemain = 2;
	for(const bool add : {false, true})
	{
		SetStackEffect partial;
		auto & changes = add ? partial.toAdd : partial.toUpdate;
		changes.emplace_back(first->unitId(), std::vector<Bonus>{dust.front(), guardian, dust.back()});
		EXPECT_THROW(apply(partial), std::invalid_argument);
		EXPECT_FALSE(first->isTimeStopped());
		EXPECT_FALSE(first->hasBonusOfType(BonusType::GUARDIAN_SPIRIT));
		EXPECT_EQ(first->guardianSpiritHitPoints, 0);
		expectDust(*first, 0);
	}
	SetStackEffect protectedPair;
	protectedPair.toAdd.emplace_back(first->unitId(), std::vector<Bonus>{guardian, dust.front(), dust.back()});
	ASSERT_NO_THROW(apply(protectedPair));
	EXPECT_TRUE(first->isTimeStopped()); EXPECT_EQ(first->guardianSpiritHitPoints, 5);
	expectDust(*first, 0);
}

class GraveDustGazeMutationTest : public GraveDustMutationTest
{
protected:
	void mapLoaded(CMap * map) override
	{
		BattleTestFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		rules.setOverrideFlag(true); map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		for(const auto & object : map->objects)
			if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get()); hero && hero->getOwner() == PlayerColor(0))
				hero->setHeroType(HeroTypeID(HeroTypeID::decode("core:ajit")));
	}
};
TEST_P(GraveDustGazeMutationTest, LaterVectorGazeRevalidatesCurrentStasisBeforeAnyPublicPacketMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(newHorizonsParalyzingGaze::captured(*battle()));
	const auto gaze = newHorizonsParalyzingGaze::marker();
	ASSERT_NO_THROW(newHorizonsParalyzingGaze::validateApplication(*battle(), first->unitId(), gaze));
	const Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	SetStackEffect invalid;
	invalid.toAdd.emplace_back(first->unitId(), std::vector<Bonus>{stopped});
	invalid.toAdd.emplace_back(first->unitId(), std::vector<Bonus>{gaze});
	invalid.toAdd.emplace_back(second->unitId(), dust);
	const auto before = first->acquireState()->save(), other = second->acquireState()->save();
	EXPECT_THROW(newHorizonsGraveDust::validateNativeEffectChanges(*battle(), invalid), std::runtime_error);
	if(GetParam() != 0) EXPECT_THROW(apply(invalid), std::runtime_error);
	EXPECT_EQ(first->acquireState()->save(), before); EXPECT_EQ(second->acquireState()->save(), other);
	EXPECT_FALSE(first->isTimeStopped()); EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*first));
	expectDust(*second, 0);
	SetStackEffect valid;
	valid.toAdd.emplace_back(first->unitId(), std::vector<Bonus>{gaze});
	valid.toAdd.emplace_back(first->unitId(), std::vector<Bonus>{stopped});
	valid.toAdd.emplace_back(second->unitId(), dust);
	ASSERT_NO_THROW(newHorizonsGraveDust::validateNativeEffectChanges(*battle(), valid));
	ASSERT_NO_THROW(apply(valid));
	EXPECT_TRUE(first->isTimeStopped()); EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*first));
	expectDust(*second, 5000);
}
class EffectPublicationServer final : public RecordingGameServer
{
public:
	size_t effectsPublished = 0;
	void applyPack(CPackForClient & pack) override
	{
		if(dynamic_cast<const SetStackEffect *>(&pack)) ++effectsPublished;
		RecordingGameServer::applyPack(pack);
	}
};
class GraveDustInitialBatchMutationTest : public GraveDustGazeMutationTest {};
TEST_P(GraveDustInitialBatchMutationTest, InitialBatchGazeRejectionPrecedesOrderedReplayAndPublication)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto gaze = newHorizonsParalyzingGaze::marker();
	const Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	battle()->addUnitBonus(first->unitId(), {stopped});
	ASSERT_TRUE(first->isTimeStopped());
	EffectPublicationServer publication;
	publication.gameState = gameState();
	CGameHandler handler(publication, gameState());
	auto * authorityRng = dynamic_cast<CRandomGenerator *>(&handler.getRandomGenerator());
	auto * fixtureRng = dynamic_cast<CRandomGenerator *>(getRNG());
	ASSERT_NE(authorityRng, nullptr);
	ASSERT_NE(fixtureRng, nullptr);
	const auto randomState = [&]()
	{
		CMemorySerializer world, fixture, original;
		world.oser & *handler.randomizer;
		fixture.oser & *fixtureRng;
		original.oser & *gameHandler->randomizer;
		return std::tuple(world.extractBuffer(), fixture.extractBuffer(), original.extractBuffer());
	};
	SetStackEffect invalid;
	invalid.battleID = battle()->getBattleID();
	invalid.toRemove.emplace_back(first->unitId(), std::vector<Bonus>{stopped});
	invalid.toAdd.emplace_back(first->unitId(), std::vector<Bonus>{gaze});
	invalid.toAdd.emplace_back(second->unitId(), newHorizonsGraveDust::records(5000, PlayerColor(0)));
	const auto before = first->acquireState()->save(), other = second->acquireState()->save();
	const auto rngBefore = randomState();
	// Ordered state admits this sequence; the existing INITIAL batch contract does not.
	ASSERT_NO_THROW(newHorizonsGraveDust::validateNativeEffectChanges(*battle(), invalid));
	ASSERT_THROW(invalid.validateGazeApplication(*battle()), std::runtime_error);
	if(GetParam() == 1) EXPECT_THROW(apply(invalid), std::runtime_error);
	else EXPECT_THROW(handler.sendAndApply(invalid), std::runtime_error);
	EXPECT_EQ(publication.effectsPublished, 0u);
	EXPECT_EQ(first->acquireState()->save(), before);
	EXPECT_EQ(second->acquireState()->save(), other);
	EXPECT_EQ(randomState(), rngBefore);
	EXPECT_TRUE(first->isTimeStopped());
	EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*first));
	expectDust(*second, 0);
	// Prove the recording boundary is reached by an admitted genuine packet.
	SetStackEffect unstop;
	unstop.battleID = battle()->getBattleID();
	unstop.toRemove.emplace_back(first->unitId(), std::vector<Bonus>{stopped});
	ASSERT_NO_THROW(handler.sendAndApply(unstop));
	ASSERT_EQ(publication.effectsPublished, 1u);
	invalid.toRemove.clear();
	ASSERT_NO_THROW(handler.sendAndApply(invalid));
	EXPECT_EQ(publication.effectsPublished, 2u);
	EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*first));
	expectDust(*second, 5000);
}
INSTANTIATE_TEST_SUITE_P(ReplicaAndAuthority, GraveDustInitialBatchMutationTest, ::testing::Values(1, 2));
INSTANTIATE_TEST_SUITE_P(NativeReplicaAuthority, GraveDustGazeMutationTest, ::testing::Values(0, 1, 2));
INSTANTIATE_TEST_SUITE_P(NativeReplicaAuthority, GraveDustMutationTest, ::testing::Values(0, 1, 2));
}
