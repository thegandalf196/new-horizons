/*
 * BattlecraftFutureExchangeTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattlecraftPreemptiveAIFixture.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"

class BattlecraftFutureExchangeTest : public battlecraftTest::PreemptiveFixture
{
protected:
	CStack * incoming = nullptr;
	CStack * defended = nullptr;
	CStack * opening = nullptr;
	CStack * reserve = nullptr;
	struct Forecast
	{
		int64_t incomingHealth;
		int64_t defendedHealth;
		int marker;
		int counters;
	};
	std::vector<std::byte> snapshot(const CBattleInfoCallback & source)
	{
		std::vector<std::byte> bytes;
		const auto append = [&](const auto & value)
		{
			const auto more = projectedHitTest::serialized(value);
			bytes.insert(bytes.end(), more.begin(), more.end());
		};
		for(const auto * unit : source.battleGetAllUnits(false))
			append(unit->acquireState()->save());
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			append(source.getBattle()->getHeroOrderStates(side));
			append(source.getBattle()->getHeroActionAllowances(side));
		}
		append(source.battleGetRound());
		append(source.battleActiveUnit() ? source.battleActiveUnit()->unitId() : std::numeric_limits<uint32_t>::max());
		append(*gameHandler->randomizer);
		append(dynamic_cast<CRandomGenerator &>(*getRNG()));
		return bytes;
	}
	void activate(const CStack * unit)
	{
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = unit->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
	}
	void prepareFuture(bool withOpening)
	{
		startGame();
		ASSERT_NO_FATAL_FAILURE(selectPreemptiveStrike(defenderSideHero));
		startBattle();
		beginCombat();
		incoming = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 1000);
		defended = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(6, 5), 1000);
		if(withOpening)
			opening = addStack(BattleSide::DEFENDER, creatureByName("core:titan"), BattleHex(1, 1), 1);
		// An untouched, distant actor keeps native execution in the SAME round,
		// so a reset cannot disguise accidental ordinary-counter consumption.
		reserve = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(15, 9), 1000);
		if(withOpening)
		{
			// Keep a normal same-round waiter, outside BOTH forecast queues' reach.
			// Initiative is separate: the opening shot must activate the incoming actor next.
			reserve->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::STACKS_MOVEMENT_RANGE, BonusSource::OTHER,
				1 - static_cast<int>(reserve->getMovementRange()), BonusSourceID()));
			reserve->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::STACKS_INITIATIVE_FLAT, BonusSource::OTHER,
				incoming->getInitiative() - 1 - reserve->getInitiative(), BonusSourceID()));
			ASSERT_EQ(reserve->getMovementRange(), 1u);
			ASSERT_TRUE(reserve->willMove());
			ASSERT_LT(reserve->getInitiative(), incoming->getInitiative());
			ASSERT_GT(BattleHex::getDistance(reserve->getPosition(), incoming->getPosition()),
				2 * reserve->getMovementRange() + 1);
		}
		removeOtherStacks({incoming, defended, opening, reserve});
		for(auto * unit : {incoming, defended, opening, reserve})
			if(unit)
			{
				forceMaximumDamage(unit);
				blockRetaliation(unit);
			}
		ASSERT_TRUE(defend(defended));
		activate(withOpening ? opening : incoming);
		if(withOpening)
		{
			std::vector<battle::Units> nativeQueue;
			battle()->battleGetTurnOrder(nativeQueue, 0, 1);
			ASSERT_EQ(nativeQueue.size(), 1u);
			ASSERT_EQ(nativeQueue.front().size(), 3u);
			ASSERT_EQ(nativeQueue.front()[0]->unitId(), opening->unitId());
			ASSERT_EQ(nativeQueue.front()[1]->unitId(), incoming->unitId());
			ASSERT_EQ(nativeQueue.front()[2]->unitId(), reserve->unitId());
		}
		initializeAI(PlayerColor(1));
		ASSERT_EQ(newHorizonsBattlecraft::preemptiveStrikeDamagePercent(
			battle()->battleGetOwnerHero(defended), defended, battle()->getRound()), 50);
		ASSERT_EQ(defended->battlecraftPreemptiveStrikeRound, -1);
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			ASSERT_TRUE(battle()->battleGetHeroOrderStates(side).empty());
		ASSERT_FALSE(std::ranges::any_of(battle()->battleAliveUnits(), [&](const auto * unit)
			{ return battle()->battleOverwatchReady(unit); }));
	}
	Forecast capture(const std::shared_ptr<HypotheticBattle> & branch)
	{
		const auto actor = branch->getForUpdate(incoming->unitId());
		const auto target = branch->getForUpdate(defended->unitId());
		return {actor->getAvailableHealth(), target->getAvailableHealth(),
			target->battlecraftPreemptiveStrikeRound, target->counterAttacks.available()};
	}
	void selected(const std::shared_ptr<HypotheticBattle> & branch, Forecast & result, float & value)
	{
		const auto actor = branch->getForUpdate(incoming->unitId());
		const auto target = branch->getForUpdate(defended->unitId());
		ASSERT_EQ(actor->getPosition(), BattleHex(5, 5));
		ASSERT_TRUE(branch->isMeleeAttackPossible(actor.get(), target.get(), actor->getPosition()));
		ASSERT_EQ(SpellTargetEvaluator::physicalTravelDistance(
			branch->getReachability(actor.get()), actor->getPosition()), 0);
		ASSERT_EQ(newHorizonsBattlecraft::preemptiveStrikeDamagePercent(
			branch->battleGetOwnerHero(target.get()), target.get(), branch->battleGetRound()), 50);
		const auto counters = target->counterAttacks.available();
		DamageCache cache;
		cache.buildDamageCache(branch, BattleSide::DEFENDER);
		const auto ap = AttackPossibility::evaluate(BattleAttackInfo(actor.get(), target.get(), 0, false),
			actor->getPosition(), cache, branch);
		ASSERT_FALSE(ap.fortuneStrikes.empty());
		const auto & first = ap.fortuneStrikes.front();
		EXPECT_EQ(first.attackerId, defended->unitId());
		EXPECT_EQ(first.defenderId, incoming->unitId());
		EXPECT_TRUE(first.retaliation); // Shared preview classification, NOT packet.counter().
		EXPECT_FALSE(first.bracePreemptive);
		BattleExchangeVariant replay;
		replay.trackAttack(ap, branch, cache);
		result = capture(branch);
		auto score = replay.getScore();
		value = score.value(); // Actor-relative, unchanged production scorer.
		EXPECT_EQ(result.marker, branch->battleGetRound());
		EXPECT_EQ(result.counters, counters);
		ASSERT_GT(result.incomingHealth, 0);
		ASSERT_GT(result.defendedHealth, 0);
	}
	void compareNative(const Forecast & expected, bool withOpening)
	{
		const auto round = battle()->getRound();
		const auto counters = defended->counterAttacks.available();
		if(withOpening)
		{
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
				BattleAction::makeShotAttack(opening, incoming)));
			ASSERT_EQ(battle()->battleActiveUnit()->unitId(), incoming->unitId());
		}
		server.attacks.clear();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeMeleeAttack(incoming, defended, incoming->getPosition(), false)));
		ASSERT_EQ(server.attacks.size(), 2u);
		for(size_t index = 0; index < 2; ++index)
		{
			const auto & packet = server.attacks[index];
			EXPECT_EQ(packet.stackAttacking, index == 0 ? defended->unitId() : incoming->unitId());
			// Actual makeAttack excludes preemptiveDamagePercent>0 from COUNTER.
			EXPECT_FALSE(packet.counter());
			ASSERT_EQ(packet.bsa.size(), 1u);
			EXPECT_EQ(packet.bsa[0].stackAttacked, index == 0 ? incoming->unitId() : defended->unitId());
		}
		EXPECT_EQ(battle()->getRound(), round);
		EXPECT_EQ(incoming->getAvailableHealth(), expected.incomingHealth);
		EXPECT_EQ(defended->getAvailableHealth(), expected.defendedHealth);
		EXPECT_EQ(defended->battlecraftPreemptiveStrikeRound, expected.marker);
		EXPECT_EQ(defended->counterAttacks.available(), counters);
		EXPECT_EQ(defended->counterAttacks.available(), expected.counters);
	}
};

TEST_F(BattlecraftFutureExchangeTest, AdjacentFuturePrimaryPreservesStandalonePreemptiveStrike)
{
	ASSERT_NO_FATAL_FAILURE(prepareFuture(false));
	const auto liveBefore = snapshot(*battle());
	Forecast expected{}, actual{};
	float selectedValue = 0;
	{
		auto parent = simulation();
		auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
		const auto before = snapshot(*parent);
		{
			DamageCache cache;
			cache.buildDamageCache(parent, BattleSide::DEFENDER);
			BattleExchangeVariant probe;
			probe.trackAttack(parent->getForUpdate(incoming->unitId()), parent->getForUpdate(defended->unitId()),
				false, false, cache, parent, true, true);
		}
		EXPECT_EQ(snapshot(*parent), before);
		EXPECT_EQ(snapshot(*sibling), before);
		EXPECT_EQ(snapshot(*battle()), liveBefore);
		{
			auto branch = std::make_shared<HypotheticBattle>(environment.get(), parent);
			branch->nextTurn(incoming->unitId(), BattleUnitTurnReason::TURN_QUEUE);
			ASSERT_NO_FATAL_FAILURE(selected(branch, expected, selectedValue));
		}
		EXPECT_EQ(snapshot(*parent), before);
		EXPECT_EQ(snapshot(*sibling), before);
		EXPECT_EQ(snapshot(*battle()), liveBefore);
		{
			auto branch = std::make_shared<HypotheticBattle>(environment.get(), parent);
			branch->nextTurn(incoming->unitId(), BattleUnitTurnReason::TURN_QUEUE);
			DamageCache cache;
			cache.buildDamageCache(branch, BattleSide::DEFENDER);
			BattleExchangeVariant future;
			future.trackAttack(branch->getForUpdate(incoming->unitId()), branch->getForUpdate(defended->unitId()),
				false, false, cache, branch);
			actual = capture(branch);
		}
		EXPECT_EQ(snapshot(*parent), before);
		EXPECT_EQ(snapshot(*sibling), before);
		EXPECT_EQ(snapshot(*battle()), liveBefore);
	}
	EXPECT_EQ(actual.incomingHealth, expected.incomingHealth) << "zero-move Battlecraft pre-hit; selected score=" << selectedValue;
	EXPECT_EQ(actual.defendedHealth, expected.defendedHealth);
	EXPECT_EQ(actual.marker, expected.marker);
	EXPECT_EQ(actual.counters, expected.counters);
	ASSERT_NO_FATAL_FAILURE(compareNative(expected, false));
}

TEST_F(BattlecraftFutureExchangeTest, RealExchangeValuesStandaloneReactionForLaterMeleeActor)
{
	ASSERT_NO_FATAL_FAILURE(prepareFuture(true));
	const auto liveBefore = snapshot(*battle());
	Forecast expected{};
	{
		auto parent = simulation();
		auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
		const auto before = snapshot(*parent);
		DamageCache cache;
		cache.buildDamageCache(parent, BattleSide::DEFENDER);
		ASSERT_TRUE(parent->battleCanShootAction(parent->battleGetUnitByID(opening->unitId()), incoming->getPosition()));
		const auto ap = AttackPossibility::evaluate(BattleAttackInfo(parent->battleGetUnitByID(opening->unitId()),
			parent->battleGetUnitByID(incoming->unitId()), 0, true), opening->getPosition(), cache, parent);
		PotentialTargets targets(parent->battleGetUnitByID(opening->unitId()), cache, parent);
		BattleExchangeEvaluator evaluator(callback->getBattle(BattleID(0)), environment, 1.0f, 1);
		evaluator.updateReachabilityMap(parent);
		const auto units = evaluator.getExchangeUnits(ap, 0, targets, parent);
		SCOPED_TRACE(([&]()
		{
			std::ostringstream trace;
			trace << "opening=" << opening->unitId() << " incoming=" << incoming->unitId()
				<< " defended=" << defended->unitId() << " reserve=" << reserve->unitId()
				<< " round=" << parent->battleGetRound() << " simulationRounds=1";
			for(const auto * live : {opening, incoming, defended, reserve})
			{
				const auto * unit = parent->battleGetUnitByID(live->unitId());
				trace << " [id=" << unit->unitId() << " liveDefending=" << live->defending
					<< " projectedDefending=" << unit->acquireState()->defending
					<< " defended=" << unit->defended() << " willMove=" << unit->willMove()
					<< " speed=" << unit->getMovementRange() << ']';
			}
			for(const auto & [round, queue] : units.units)
			{
				trace << " queue" << static_cast<int>(round) << '=';
				for(const auto * unit : queue) trace << unit->unitId() << ',';
			}
			trace << " melee="; for(const auto * unit : units.melleeAccessible) trace << unit->unitId() << ',';
			trace << " shooters="; for(const auto * unit : units.shooters) trace << unit->unitId() << ',';
			return trace.str();
		}()));
		ASSERT_EQ(units.units.size(), 2u);
		ASSERT_EQ(units.units.at(0).size(), 2u);
		ASSERT_EQ(units.units.at(0)[0]->unitId(), opening->unitId());
		ASSERT_EQ(units.units.at(0)[1]->unitId(), incoming->unitId());
		ASSERT_TRUE(vstd::contains(units.shooters, parent->battleGetUnitByID(opening->unitId())));
		ASSERT_TRUE(vstd::contains(units.melleeAccessible, parent->battleGetUnitByID(defended->unitId())));
		ASSERT_TRUE(std::ranges::none_of(units.melleeAccessible, [&](const auto * unit)
			{ return unit->unitId() == reserve->unitId(); }));
		const auto actual = evaluator.evaluateExchange(ap, 0, targets, cache, parent);
		{
			auto reference = std::make_shared<HypotheticBattle>(environment.get(), parent);
			BattleExchangeVariant first;
			first.trackAttack(ap, reference, cache);
			reference->nextTurn(incoming->unitId(), BattleUnitTurnReason::TURN_QUEUE);
			float secondValue = 0;
			ASSERT_NO_FATAL_FAILURE(selected(reference, expected, secondValue));
			auto firstScore = first.getScore();
			// Existing one-round/two-queue scaling, independent selected APs only.
			EXPECT_FLOAT_EQ(actual, (firstScore.value() - secondValue) / 2.0f);
		}
		EXPECT_EQ(snapshot(*parent), before);
		EXPECT_EQ(snapshot(*sibling), before);
		EXPECT_EQ(snapshot(*battle()), liveBefore);
	}
	ASSERT_NO_FATAL_FAILURE(compareNative(expected, true));
}
