/*
 * NewHorizonsFrozenTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleTestFixture.h"

#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/NewHorizonsFrozen.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/mapObjects/army/CArmedInstance.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

class NewHorizonsFrozenServerTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void prepare()
	{
		startGame();
		startBattle();
		BattleNextRound round;
		round.battleID = BattleID(0);
		gameHandler->sendAndApply(round);
	}

	CStack * unit(BattleSide side, const char * id, int hex, int count)
	{
		auto * result = addStack(side, creatureByName(id), BattleHex(hex), count);
		// Remove unrelated chance draws without changing the Frozen proc chance.
		SetStackEffect controls;
		controls.battleID = BattleID(0);
		controls.toAdd.emplace_back(result->unitId(), std::vector<Bonus>{
			Bonus(BonusDuration::ONE_BATTLE, BonusType::NO_LUCK, BonusSource::OTHER, 1, BonusSourceID()),
			Bonus(BonusDuration::ONE_BATTLE, BonusType::NO_MORALE, BonusSource::OTHER, 1, BonusSourceID()),
			Bonus(BonusDuration::ONE_BATTLE, BonusType::ALWAYS_MAXIMUM_DAMAGE, BonusSource::OTHER, 1, BonusSourceID())});
		gameHandler->sendAndApply(controls);
		return result;
	}

	void activate(const CStack * source)
	{
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = source->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
	}

	void freeze(const CStack * target)
	{
		SetStackEffect effect;
		effect.battleID = BattleID(0);
		effect.toAdd.emplace_back(target->unitId(), std::vector<Bonus>{
			newHorizonsFrozen::makeFrozenMarker(BonusSourceID(creatureByName("core:iceElemental")), battle()->getRound())});
		gameHandler->sendAndApply(effect);
	}

	int counters(const CStack * source) const
	{
		return std::ranges::count_if(server.attacks, [source](const auto & packet)
		{
			return packet.counter() && packet.stackAttacking == source->unitId();
		});
	}

	int successfulProcSeed() const
	{
		for(int seed = 0; seed < 4096; ++seed)
		{
			CRandomGenerator candidate(seed);
			if(candidate.nextInt(0, 99) < 20)
				return seed;
		}
		throw std::runtime_error("No deterministic Frozen proc seed");
	}

	void seedFrozenProc(int seed)
	{
		// rollAttackFlags checks even zero-chance combat abilities. The first
		// check for an army initializes its private ability RNG using a global
		// draw before the zero-chance early return. Prime only that cache before
		// reseeding the global stream, leaving Frozen's real 20% draw isolated.
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			ASSERT_FALSE(gameHandler->randomizer->rollCombatAbility(battle()->getSideArmy(side)->id, 0));
		gameHandler->randomizer->setSeed(seed);
	}
};

TEST_F(NewHorizonsFrozenServerTest, AcceptedIceMeleeFreezesBeforeRetaliation)
{
	prepare();
	ASSERT_EQ(newHorizonsFrozen::chancePercent(battle()->getMagicRules()), 20);
	auto * ice = unit(BattleSide::ATTACKER, "core:iceElemental", leftHex, 5);
	auto * target = unit(BattleSide::DEFENDER, "core:pikeman", rightHex, 500);
	activate(ice);
	const auto round = battle()->getRound();
	const auto before = target->getAvailableHealth();
	ASSERT_TRUE(newHorizonsFrozen::canApply(*target->acquireState(), round));
	const auto procSeed = successfulProcSeed();
	CRandomGenerator expectedRandom(procSeed);
	expectedRandom.nextInt(0, 99);
	seedFrozenProc(procSeed);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(ice, target->getPosition(), ice->getPosition())));
	EXPECT_LT(target->getAvailableHealth(), before);
	EXPECT_TRUE(target->alive());
	EXPECT_EQ(target->frozenLastAppliedRound(), round);
	EXPECT_EQ(counters(target), 0);
	EXPECT_EQ(gameHandler->getRandomGenerator().nextInt(), expectedRandom.nextInt());
	// Scheduling may already have forfeited/thawed this target. Either way the
	// receipt prevents reapplication during this round, including after Cure.
	EXPECT_FALSE(newHorizonsFrozen::canApply(*target->acquireState(), round));
}

TEST_F(NewHorizonsFrozenServerTest, PhysicalShatterUsesFinalDamageAndNeverAllowsItsRetaliation)
{
	prepare();
	ASSERT_EQ(newHorizonsFrozen::shatterBonusPercent(battle()->getMagicRules()), 25);
	auto * source = unit(BattleSide::ATTACKER, "core:pikeman", leftHex, 20);
	auto * target = unit(BattleSide::DEFENDER, "core:pikeman", rightHex, 500);
	activate(source);
	const BattleAttackInfo ordinary(source, target, 0, false);
	const auto baseline = battle()->calculateDmgRange(ordinary).damage;
	freeze(target);
	const auto forecast = battle()->calculateDmgRange(ordinary).damage;
	EXPECT_EQ(forecast.min, baseline.min * 125 / 100);
	EXPECT_EQ(forecast.max, baseline.max * 125 / 100);
	const auto before = target->getAvailableHealth();
	const auto receiptRound = target->frozenLastAppliedRound();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(source, target->getPosition(), source->getPosition())));
	EXPECT_EQ(before - target->getAvailableHealth(), forecast.max);
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_EQ(target->frozenLastAppliedRound(), receiptRound);
	EXPECT_EQ(counters(target), 0);
	ASSERT_FALSE(server.attacks.empty());
	const auto shatterHit = std::ranges::find_if(server.attacks.front().bsa, [target](const auto & hit)
	{
		return hit.stackAttacked == target->unitId();
	});
	ASSERT_NE(shatterHit, server.attacks.front().bsa.end());
	EXPECT_TRUE(shatterHit->shattered());
}

TEST_F(NewHorizonsFrozenServerTest, NormalForfeitureThawsButPreservesRoundReceipt)
{
	prepare();
	auto * target = unit(BattleSide::ATTACKER, "core:pikeman", leftHex, 20);
	unit(BattleSide::DEFENDER, "core:pikeman", rightHex, 500);
	activate(target);
	freeze(target);
	const auto round = battle()->getRound();
	ASSERT_FALSE(target->canMove());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeNoAction(target)));
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_EQ(target->frozenLastAppliedRound(), round);
	EXPECT_TRUE(target->moved());
	EXPECT_FALSE(target->defended());
	EXPECT_FALSE(newHorizonsFrozen::canApply(*target->acquireState(), round));
}

TEST_F(NewHorizonsFrozenServerTest, IceRetaliationFreezesCurrentAttackerWithoutThawingItsSpentAction)
{
	prepare();
	auto * source = unit(BattleSide::ATTACKER, "core:pikeman", leftHex, 1000);
	auto * ice = unit(BattleSide::DEFENDER, "core:iceElemental", rightHex, 300);
	SetStackEffect extraBlow;
	extraBlow.battleID = BattleID(0);
	extraBlow.toAdd.emplace_back(source->unitId(), std::vector<Bonus>{
		Bonus(BonusDuration::ONE_BATTLE, BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID())});
	gameHandler->sendAndApply(extraBlow);
	activate(source);
	const auto round = battle()->getRound();
	seedFrozenProc(successfulProcSeed());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(source, ice->getPosition(), source->getPosition())));
	ASSERT_TRUE(source->alive());
	ASSERT_TRUE(ice->alive());
	EXPECT_EQ(counters(ice), 1);
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*source));
	EXPECT_EQ(source->frozenLastAppliedRound(), round);
	EXPECT_TRUE(source->moved());
	EXPECT_FALSE(source->ableToRetaliate());
	EXPECT_EQ(std::ranges::count_if(server.attacks, [source](const auto & packet)
	{
		return packet.stackAttacking == source->unitId() && !packet.counter();
	}), 1);
}
