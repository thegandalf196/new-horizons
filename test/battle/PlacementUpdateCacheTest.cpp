/*
 * PlacementUpdateCacheTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/PreparedBattleWorldCommit.h"
#include "../../lib/battle/ProjectedBattleWorldArena.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/gameState/ReplayLog.h"

namespace
{
class PlacementUpdateCacheTest : public HeroCommandFixture
{
protected:
	CStack * moving = nullptr;
	CStack * stationary = nullptr;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		startGame();
		startBattle();
		moving = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(89), 4);
		stationary = addStack(BattleSide::DEFENDER, CreatureID(0), BattleHex(93), 4);
	}
	int speed(const battle::Unit & unit)
	{
		return unit.valOfBonuses(Selector::type()(BonusType::STACKS_SPEED));
	}
	JsonNode at(int hex)
	{
		auto state = moving->acquireState();
		state->setPosition(BattleHex(hex));
		return state->save();
	}
	void installPlacementBonuses()
	{
		auto own = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::STACKS_SPEED, BonusSource::OTHER, 4, BonusSourceID());
		own->limiter = std::make_shared<UnitOnHexLimiter>(BattleHexArray{BattleHex(92)});
		moving->addNewBonus(own);
		auto adjacent = std::make_shared<UnitAdjacentLimiter>();
		adjacent->targetUnit = moving->creatureId();
		auto other = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::STACKS_SPEED, BonusSource::OTHER, 3, BonusSourceID());
		other->limiter = adjacent;
		stationary->addNewBonus(other);
	}
	BattleUnitsChanged packet(const JsonNode & data)
	{
		BattleUnitsChanged result;
		result.battleID = battle()->getBattleID();
		UnitChanges change(moving->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = data;
		change.healthDelta = 0;
		result.changedStacks.push_back(std::move(change));
		return result;
	}
	void prepare(battle::PreparedBattleWorldCommit & owner)
	{
		TNodesVector authority;
		// This single-threaded fixture owns all mutable source/library exclusion.
		// Production must obtain actual authority, not infer it from these casts.
		for(const auto * node : owner.projected().capturedGraphNodes())
			authority.push_back(const_cast<CBonusSystemNode *>(node));
		owner.prepare(authority);
	}
};

TEST_F(PlacementUpdateCacheTest, WarmOwnAndOtherRecipientCachesGainAndLosePlacementBenefits)
{
	installPlacementBonuses();
	const int ownBefore = speed(*moving), otherBefore = speed(*stationary);
	const auto original = moving->save();
	const auto moved = at(92);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	battle()->updateUnit(moving->unitId(), moved, 0);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation + 1);
	EXPECT_EQ(speed(*moving), ownBefore + 4);
	EXPECT_EQ(speed(*stationary), otherBefore + 3);
	const auto arrived = PreparedBonusGraphStorage::currentGeneration();
	battle()->updateUnit(moving->unitId(), original, 0);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), arrived + 1);
	EXPECT_EQ(speed(*moving), ownBefore);
	EXPECT_EQ(speed(*stationary), otherBefore);
}

TEST_F(PlacementUpdateCacheTest, UnchangedPositionAndZeroHealthDoNotInvalidateTheBattleRoot)
{
	installPlacementBonuses();
	const int ownBefore = speed(*moving), otherBefore = speed(*stationary);
	const auto unchanged = moving->save();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	battle()->updateUnit(moving->unitId(), unchanged, 0);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_EQ(moving->save(), unchanged);
	EXPECT_EQ(speed(*moving), ownBefore);
	EXPECT_EQ(speed(*stationary), otherBefore);
}

TEST_F(PlacementUpdateCacheTest, DetachedPlacementQueriesDoNotChangeNativeCachesOrSiblingWorld)
{
	installPlacementBonuses();
	const int ownBefore = speed(*moving), otherBefore = speed(*stationary);
	const auto moved = at(92);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	int ownAfter = 0, otherAfter = 0;
	{
		battle::ProjectedBattleWorldArena changed(*battle()), sibling(*battle());
		ASSERT_EQ(changed.updateUnit(moving->unitId(), moved, 0),
			battle::ProjectedBattleWorldArena::UpdateDecision::APPLY);
		ownAfter = speed(*changed.findUnit(moving->unitId()));
		otherAfter = speed(*changed.findUnit(stationary->unitId()));
		EXPECT_EQ(ownAfter, ownBefore + 4);
		EXPECT_EQ(otherAfter, otherBefore + 3);
		EXPECT_EQ(speed(*sibling.findUnit(moving->unitId())), ownBefore);
		EXPECT_EQ(speed(*sibling.findUnit(stationary->unitId())), otherBefore);
		EXPECT_EQ(speed(*moving), ownBefore);
		EXPECT_EQ(speed(*stationary), otherBefore);
		EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	}
	// No projected provider survives the ordinary scalar UPDATE oracle.
	battle()->updateUnit(moving->unitId(), moved, 0);
	EXPECT_EQ(speed(*moving), ownAfter);
	EXPECT_EQ(speed(*stationary), otherAfter);
}

TEST_F(PlacementUpdateCacheTest, PreparedPlacementCommitReplaysExactNativeGenerationDelta)
{
	installPlacementBonuses();
	const auto original = moving->save(), moved = at(92);
	const auto beforeNative = PreparedBonusGraphStorage::currentGeneration();
	battle()->updateUnit(moving->unitId(), moved, 0);
	const auto nativeDelta = PreparedBonusGraphStorage::currentGeneration() - beforeNative;
	const int ownAfter = speed(*moving), otherAfter = speed(*stationary);
	battle()->updateUnit(moving->unitId(), original, 0);
	EXPECT_NE(speed(*moving), ownAfter);
	EXPECT_NE(speed(*stationary), otherAfter);
	gameHandler->gs->replayLog.configure(false, 2);
	gameHandler->gs->replayLog.beginDay(gameHandler->gs->day, {std::byte{0x11}});
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	battle::PreparedBattleWorldCommit owner(gameHandler->gs,
		std::unique_lock<std::shared_mutex>(CGameState::mutex), packet(moved));
	EXPECT_EQ(speed(*owner.projected().findUnit(moving->unitId())), ownAfter);
	EXPECT_EQ(speed(*owner.projected().findUnit(stationary->unitId())), otherAfter);
	prepare(owner);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	ASSERT_TRUE(owner.readyToCommit());
	ASSERT_TRUE(owner.commitSimulation());
	EXPECT_EQ(nativeDelta, 1);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation + nativeDelta);
	EXPECT_EQ(speed(*moving), ownAfter);
	EXPECT_EQ(speed(*stationary), otherAfter);
	EXPECT_EQ(moving->save(), moved);
}

TEST_F(PlacementUpdateCacheTest, PreparedUnchangedPositionHasNoSpuriousGenerationEvent)
{
	installPlacementBonuses();
	const int ownBefore = speed(*moving), otherBefore = speed(*stationary);
	gameHandler->gs->replayLog.configure(false, 2);
	gameHandler->gs->replayLog.beginDay(gameHandler->gs->day, {std::byte{0x11}});
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	battle::PreparedBattleWorldCommit owner(gameHandler->gs,
		std::unique_lock<std::shared_mutex>(CGameState::mutex), packet(moving->save()));
	prepare(owner);
	ASSERT_TRUE(owner.readyToCommit());
	ASSERT_TRUE(owner.commitSimulation());
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_EQ(speed(*moving), ownBefore);
	EXPECT_EQ(speed(*stationary), otherBefore);
}

TEST_F(PlacementUpdateCacheTest, PositionOnlyUpdateDoesNotRunDamageExpiry)
{
	auto marker = std::make_shared<Bonus>(BonusDuration::UNTIL_BEING_ATTACKED,
		BonusType::STACKS_SPEED, BonusSource::OTHER, 2, BonusSourceID());
	moving->addNewBonus(marker);
	const int before = speed(*moving);
	const auto moved = at(92);
	{
		battle::ProjectedBattleWorldArena projected(*battle());
		ASSERT_EQ(projected.updateUnit(moving->unitId(), moved, 0),
			battle::ProjectedBattleWorldArena::UpdateDecision::APPLY);
		EXPECT_EQ(speed(*projected.findUnit(moving->unitId())), before);
	}
	battle()->updateUnit(moving->unitId(), moved, 0);
	EXPECT_EQ(speed(*moving), before);
	EXPECT_TRUE(vstd::contains(moving->getExportedBonusList(), marker));
}
}
