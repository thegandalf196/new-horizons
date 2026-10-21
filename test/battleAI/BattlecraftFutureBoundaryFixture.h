/*
 * BattlecraftFutureBoundaryFixture.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "BattlecraftPreemptiveAIFixture.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../lib/GameSettings.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/mapping/CMap.h"
#include <tuple>

namespace battlecraftTest
{
class FutureBoundaryFixture : public PreemptiveFixture
{
protected:
	CStack * actor = nullptr;
	CStack * target = nullptr;
	CStack * reserve = nullptr;
	CStack * opening = nullptr;
	CStack * protector = nullptr;
	BattleHex from;
	bool ranged = false;
	using UnitView = std::tuple<int64_t, int, int, int, int64_t, int>;
	using Views = std::vector<UnitView>;
	struct Result
	{
		Views units;
		std::vector<std::pair<uint32_t, uint32_t>> strikes;
		float value = 0;
	};

	void mapLoaded(CMap * loaded) override
	{
		PreemptiveFixture::mapLoaded(loaded);
		const JsonNode file(JsonPath::builtin("config/newHorizonsCombat"));
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, file["combat"]["heroCommands"]);
	}

	void activate(const CStack * unit)
	{
		BattleSetActiveStack pack;
		pack.battleID = BattleID(0);
		pack.stack = unit->unitId();
		pack.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(pack);
	}

	void prepare(bool perk = true, bool defending = true, bool shooting = false,
		bool moved = false, bool withOpening = false)
	{
		startGame();
		if(perk)
		{
			ASSERT_NO_FATAL_FAILURE(selectPreemptiveStrike(defenderSideHero));
		}
		startBattle();
		beginCombat();
		ranged = shooting;
		actor = addStack(BattleSide::ATTACKER, creatureByName(shooting ? "core:titan" : "core:pikeman"),
			shooting ? BattleHex(1, 1) : moved ? BattleHex(89) : BattleHex(5, 5), shooting ? 2 : 1000);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"),
			moved ? BattleHex(93) : BattleHex(6, 5), 1000);
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
				actor->getInitiative() - 1 - reserve->getInitiative(), BonusSourceID()));
			ASSERT_EQ(reserve->getMovementRange(), 1u);
			ASSERT_TRUE(reserve->willMove());
			ASSERT_LT(reserve->getInitiative(), actor->getInitiative());
			ASSERT_GT(BattleHex::getDistance(reserve->getPosition(), actor->getPosition()),
				2 * reserve->getMovementRange() + 1);
		}
		if(withOpening)
			opening = addStack(BattleSide::DEFENDER, creatureByName("core:titan"), BattleHex(1, 1), 1);
		removeOtherStacks({actor, target, reserve, opening});
		for(auto * unit : {actor, target, reserve, opening})
			if(unit)
			{
				forceMaximumDamage(unit);
				blockRetaliation(unit);
			}
		if(defending)
		{
			ASSERT_TRUE(defend(target));
		}
		activate(withOpening ? opening : actor);
		if(withOpening)
		{
			std::vector<battle::Units> nativeQueue;
			battle()->battleGetTurnOrder(nativeQueue, 0, 1);
			ASSERT_EQ(nativeQueue.size(), 1u);
			ASSERT_EQ(nativeQueue.front().size(), 3u);
			ASSERT_EQ(nativeQueue.front()[0]->unitId(), opening->unitId());
			ASSERT_EQ(nativeQueue.front()[1]->unitId(), actor->unitId());
			ASSERT_EQ(nativeQueue.front()[2]->unitId(), reserve->unitId());
		}
		initializeAI(PlayerColor(1));
		from = moved ? BattleHex(92) : actor->getPosition();
		ASSERT_EQ(ready(target), perk && defending ? 50 : 0);
		ASSERT_FALSE(std::ranges::any_of(battle()->battleAliveUnits(), [&](const auto * unit)
			{ return battle()->battleOverwatchReady(unit); }));
	}

	int ready(const battle::Unit * unit) const
	{
		return newHorizonsBattlecraft::preemptiveStrikeDamagePercent(
			battle()->battleGetOwnerHero(unit), unit, battle()->getRound());
	}

	void extraAttack()
	{
		actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
		ASSERT_EQ(AttackPossibility::getAttackCount(*actor, ranged, *battle()), 2);
	}

	std::vector<std::byte> snapshot(const CBattleInfoCallback & state)
	{
		std::vector<std::byte> bytes;
		const auto append = [&](const auto & value)
		{
			const auto more = projectedHitTest::serialized(value);
			bytes.insert(bytes.end(), more.begin(), more.end());
		};
		for(const auto * unit : state.battleGetAllUnits(false))
			append(unit->acquireState()->save());
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			append(state.getBattle()->getHeroOrderStates(side));
			append(state.getBattle()->getHeroActionAllowances(side));
		}
		append(state.battleGetRound());
		append(state.battleActiveUnit() ? state.battleActiveUnit()->unitId() : std::numeric_limits<uint32_t>::max());
		append(*gameHandler->randomizer);
		append(dynamic_cast<CRandomGenerator &>(*getRNG()));
		return bytes;
	}

	Views views(const CBattleInfoCallback & state) const
	{
		Views result;
		for(const auto * live : {actor, target, reserve, opening, protector})
			if(live)
			{
				const auto unit = state.battleGetUnitByID(live->unitId())->acquireState();
				result.emplace_back(unit->getAvailableHealth(), unit->getPosition().toInt(),
					unit->battlecraftPreemptiveStrikeRound, unit->counterAttacks.available(),
					unit->guardianSpiritHitPoints, unit->shots.available());
			}
		return result;
	}

	void selected(const std::shared_ptr<HypotheticBattle> & branch, Result & result)
	{
		const auto incoming = branch->getForUpdate(actor->unitId());
		const auto victim = branch->getForUpdate(target->unitId());
		const auto reach = branch->getReachability(incoming.get());
		const int distance = ranged ? 0 : SpellTargetEvaluator::physicalTravelDistance(reach, from);
		ASSERT_GE(distance, 0);
		ASSERT_TRUE(ranged ? branch->battleCanShootAction(incoming.get(), victim->getPosition())
			: branch->isMeleeAttackPossibleWithLongReach(incoming.get(), victim.get(), from));
		DamageCache cache;
		cache.buildDamageCache(branch, BattleSide::DEFENDER);
		const auto ap = AttackPossibility::evaluate(
			BattleAttackInfo(incoming.get(), victim.get(), distance, ranged), from, cache, branch);
		for(const auto & strike : ap.fortuneStrikes)
		{
			ASSERT_EQ(strike.hits.size(), 1u);
			result.strikes.emplace_back(strike.attackerId, strike.hits.front().first);
		}
		if(ap.fortuneStrikes.empty())
		{
			// Legacy single-hit previews need no effect replay metadata. Never
			// accept missing metadata as a reference for an eligible pre-hit.
			ASSERT_TRUE(ranged || (ready(target) == 0 && (!protector || ready(protector) == 0)));
			ASSERT_EQ(AttackPossibility::getAttackCount(*incoming, ranged, *branch), 1);
			result.strikes.emplace_back(actor->unitId(), target->unitId());
		}
		BattleExchangeVariant replay;
		replay.trackAttack(ap, branch, cache);
		auto score = replay.getScore();
		result.value = score.value();
		result.units = views(*branch);
	}

	void native(const Result & expected)
	{
		const auto round = battle()->getRound();
		server.attacks.clear();
		const auto action = ranged ? BattleAction::makeShotAttack(actor, target)
			: BattleAction::makeMeleeAttack(actor, target, from, false);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		ASSERT_EQ(server.attacks.size(), expected.strikes.size());
		for(size_t i = 0; i < expected.strikes.size(); ++i)
		{
			EXPECT_EQ(server.attacks[i].stackAttacking, expected.strikes[i].first);
			ASSERT_EQ(server.attacks[i].bsa.size(), 1u);
			EXPECT_EQ(server.attacks[i].bsa.front().stackAttacked, expected.strikes[i].second);
			EXPECT_FALSE(server.attacks[i].counter()); // Ordinary counters are blocked; pre-hits are not packet COUNTER.
		}
		EXPECT_EQ(battle()->getRound(), round);
		EXPECT_EQ(views(*battle()), expected.units);
	}

	void compare(bool completedAction, bool allowRetaliation = true)
	{
		const auto liveBefore = snapshot(*battle());
		Result expected;
		{
			auto parent = simulation();
			parent->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
			auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
			const auto before = snapshot(*parent);
			{
				auto reference = std::make_shared<HypotheticBattle>(environment.get(), parent);
				ASSERT_NO_FATAL_FAILURE(selected(reference, expected));
			}
			DamageCache cache;
			cache.buildDamageCache(parent, BattleSide::DEFENDER);
			BattleExchangeVariant probe;
			BattleExchangeAttackResult probeResult;
			probe.trackAttack(parent->getForUpdate(actor->unitId()), parent->getForUpdate(target->unitId()),
				ranged, true, cache, parent, true, allowRetaliation, nullptr, nullptr, nullptr, &probeResult);
			EXPECT_FALSE(probeResult.completedAction);
			EXPECT_EQ(snapshot(*parent), before);
			EXPECT_EQ(snapshot(*sibling), before);
			{
				auto branch = std::make_shared<HypotheticBattle>(environment.get(), parent);
				BattleExchangeVariant future;
				BattleExchangeAttackResult actionResult;
				future.trackAttack(branch->getForUpdate(actor->unitId()), branch->getForUpdate(target->unitId()),
					ranged, true, cache, branch, false, allowRetaliation, nullptr, nullptr, nullptr, &actionResult);
				EXPECT_EQ(actionResult.completedAction, completedAction);
				EXPECT_EQ(views(*branch), expected.units);
			}
			EXPECT_EQ(snapshot(*parent), before);
			EXPECT_EQ(snapshot(*sibling), before);
			EXPECT_EQ(snapshot(*battle()), liveBefore);
		}
		ASSERT_NO_FATAL_FAILURE(native(expected));
	}
};
}
