/*
 * LegacyOrderOracleChecks.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/CStack.h"
#include <optional>

namespace legacyOrderTest
{
struct Context
{
	BattleTestFixture & fixture;
	CGameHandler & handler;
	std::shared_ptr<Environment> environment;
	std::shared_ptr<CBattleCallback> callback;
	CStack * own;
	CStack * enemy;
	CRandomGenerator & fixtureRng;
	RecordingGameServer & packets;
	Context(BattleTestFixture & fixture, std::shared_ptr<Environment> environment,
		std::shared_ptr<CBattleCallback> callback, CStack * own, CStack * enemy)
		: fixture(fixture), handler(*fixture.gameHandler), environment(std::move(environment)),
		callback(std::move(callback)), own(own), enemy(enemy),
		fixtureRng(dynamic_cast<CRandomGenerator &>(*fixture.getRNG())), packets(fixture.server)
	{}
};

inline void deterministicUnits(Context & c)
{
	for(auto * unit : {c.own, c.enemy})
	{
		BattleTestFixture::forceMaximumDamage(unit);
		BattleTestFixture::blockRetaliation(unit);
	}
	c.fixture.addStack(BattleSide::ATTACKER, BattleTestFixture::creatureByName("core:pikeman"),
		BattleHex(15, 9), 1000); // Keep native checks within the same round.
}

inline std::vector<std::byte> snapshot(Context & c, const CBattleInfoCallback & state)
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
	append(*c.handler.randomizer);
	append(c.fixtureRng);
	return bytes;
}

struct Exchange
{
	int64_t actorHealth = 0;
	int64_t victimHealth = 0;
	int victimCounters = 0;
	float value = 0;
};

inline void preview(const std::shared_ptr<HypotheticBattle> & branch,
	uint32_t actorId, uint32_t victimId, BattleHex from, Exchange & result)
{
	const auto actor = branch->getForUpdate(actorId);
	const auto victim = branch->getForUpdate(victimId);
	const int distance = SpellTargetEvaluator::physicalTravelDistance(branch->getReachability(actor.get()), from);
	ASSERT_GE(distance, 0);
	ASSERT_TRUE(branch->isMeleeAttackPossibleWithLongReach(actor.get(), victim.get(), from));
	DamageCache cache;
	cache.buildDamageCache(branch, actor->unitSide());
	const auto ap = AttackPossibility::evaluate(
		BattleAttackInfo(actor.get(), victim.get(), distance, false), from, cache, branch);
	BattleExchangeVariant replay;
	replay.trackAttack(ap, branch, cache);
	auto score = replay.getScore();
	result = {actor->getAvailableHealth(), victim->getAvailableHealth(), victim->counterAttacks.available(), score.value()};
}

inline void position(Context & c, CStack * unit, BattleHex hex)
{
	const auto state = unit->acquireState();
	state->setPosition(hex);
	BattleUnitsChanged changed;
	changed.battleID = BattleID(0);
	changed.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::UPDATE);
	changed.changedStacks.back().data = state->save();
	c.handler.sendAndApply(changed);
}

inline void expectBracePercent(const CBattleInfoCallback & view, const Context & c, int percent)
{
	BattleAttackInfo blow(view.battleGetUnitByID(c.own->unitId()),
		view.battleGetUnitByID(c.enemy->unitId()), 0, false);
	const auto normal = view.battleEstimateDamage(blow);
	blow.bracePreemptive = true;
	const auto preemptive = view.battleEstimateDamage(blow);
	EXPECT_EQ(preemptive.damage.min, normal.damage.min * percent / 100);
	EXPECT_EQ(preemptive.damage.max, normal.damage.max * percent / 100);
}

inline void brace(Context c, std::optional<int> expectedPercent = std::nullopt)
{
	deterministicUnits(c);
	ASSERT_NO_FATAL_FAILURE(position(c, c.own, BattleHex(93)));
	ASSERT_NO_FATAL_FAILURE(position(c, c.enemy, BattleHex(89)));
	const auto live = c.callback->getBattle(BattleID(0));
	const auto before = snapshot(c, *live);
	const auto prepared = live->battlePrepareHeroOrderState(BattleSide::ATTACKER, HeroCommand::BRACE, {});
	ASSERT_TRUE(prepared);
	Exchange ordinary, braced;
	{
		auto parent = std::make_shared<HypotheticBattle>(c.environment.get(), live);
		auto sibling = std::make_shared<HypotheticBattle>(c.environment.get(), parent);
		const auto parentBefore = snapshot(c, *parent);
		ASSERT_EQ(SpellTargetEvaluator::physicalTravelDistance(parent->getReachability(c.enemy), BattleHex(92)), 3);
		{
			auto reference = std::make_shared<HypotheticBattle>(c.environment.get(), parent);
			ASSERT_NO_FATAL_FAILURE(preview(reference, c.enemy->unitId(), c.own->unitId(), BattleHex(92), ordinary));
		}
		{
			auto reference = std::make_shared<HypotheticBattle>(c.environment.get(), parent);
			reference->setHeroOrderState(BattleSide::ATTACKER, prepared);
			ASSERT_TRUE(reference->battleCanTriggerHeroOrderBrace(c.enemy, c.own, 3, false, false));
			if(expectedPercent)
				expectBracePercent(*reference, c, *expectedPercent);
			ASSERT_NO_FATAL_FAILURE(preview(reference, c.enemy->unitId(), c.own->unitId(), BattleHex(92), braced));
		}
		// Common unit-exchange value, from the incoming enemy's perspective;
		// this is not a final all-Hero-candidates winner or a raw HP heuristic.
		EXPECT_LT(braced.value, ordinary.value);
		EXPECT_LT(braced.actorHealth, ordinary.actorHealth);
		ASSERT_GT(braced.actorHealth, 0);
		EXPECT_EQ(snapshot(c, *parent), parentBefore);
		EXPECT_EQ(snapshot(c, *sibling), parentBefore);
		EXPECT_EQ(snapshot(c, *live), before);
	}
	ASSERT_TRUE(c.handler.battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::BRACE)));
	ASSERT_TRUE(live->battleCanTriggerHeroOrderBrace(c.enemy, c.own, 3, false, false));
	if(expectedPercent)
		expectBracePercent(*live, c, *expectedPercent);
	BattleSetActiveStack active;
	active.battleID = BattleID(0);
	active.stack = c.enemy->unitId();
	active.reason = BattleUnitTurnReason::TURN_QUEUE;
	c.handler.sendAndApply(active);
	const auto round = live->battleGetRound();
	c.packets.attacks.clear();
	ASSERT_TRUE(c.handler.battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeMeleeAttack(c.enemy, c.own, BattleHex(92), false)));
	ASSERT_EQ(c.packets.attacks.size(), 2u);
	EXPECT_EQ(c.packets.attacks[0].stackAttacking, c.own->unitId());
	EXPECT_EQ(c.packets.attacks[1].stackAttacking, c.enemy->unitId());
	ASSERT_EQ(c.packets.attacks[0].bsa.size(), 1u);
	ASSERT_EQ(c.packets.attacks[1].bsa.size(), 1u);
	EXPECT_EQ(c.packets.attacks[0].bsa.front().stackAttacked, c.enemy->unitId());
	EXPECT_EQ(c.packets.attacks[1].bsa.front().stackAttacked, c.own->unitId());
	EXPECT_FALSE(c.packets.attacks[0].counter());
	EXPECT_FALSE(c.packets.attacks[1].counter());
	EXPECT_EQ(c.enemy->getAvailableHealth(), braced.actorHealth);
	EXPECT_EQ(c.own->getAvailableHealth(), braced.victimHealth);
	EXPECT_EQ(c.own->counterAttacks.available(), braced.victimCounters);
	EXPECT_EQ(live->battleGetRound(), round);
	// Admission queries, NOT a second native attack: Brace lasts until round end.
	EXPECT_TRUE(live->battleCanTriggerHeroOrderBrace(c.enemy, c.own, 3, false, false));
	EXPECT_FALSE(live->battleCanTriggerHeroOrderBrace(c.enemy, c.own, 0, false, false));
	EXPECT_FALSE(live->battleCanTriggerHeroOrderBrace(c.enemy, c.own, 2, false, false));
	EXPECT_FALSE(live->battleCanTriggerHeroOrderBrace(c.enemy, c.own, 3, true, false));
	EXPECT_FALSE(live->battleCanTriggerHeroOrderBrace(c.enemy, c.own, 3, false, true));
}

inline void adjacentCharge(Context c)
{
	deterministicUnits(c);
	ASSERT_FALSE(c.fixture.attackerSideHero->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.countercharge"));
	ASSERT_TRUE(c.own->hasBonusOfType(BonusType::FLYING));
	ASSERT_TRUE(c.enemy->hasBonusOfType(BonusType::FLYING));
	const auto live = c.callback->getBattle(BattleID(0));
	const auto before = snapshot(c, *live);
	const auto prepared = live->battlePrepareHeroOrderState(BattleSide::ATTACKER, HeroCommand::CHARGE, {});
	ASSERT_TRUE(prepared);
	Exchange expected;
	BattleHex nativeFrom;
	{
		auto parent = std::make_shared<HypotheticBattle>(c.environment.get(), live);
		auto sibling = std::make_shared<HypotheticBattle>(c.environment.get(), parent);
		const auto parentBefore = snapshot(c, *parent);
		DamageCache cache;
		cache.buildDamageCache(parent, BattleSide::ATTACKER);
		PotentialTargets targets(c.own, cache, parent);
		ASSERT_FALSE(targets.possibleAttacks.empty());
		for(const auto & action : targets.possibleAttacks)
		{
			ASSERT_EQ(action.attack.defender->unitId(), c.enemy->unitId());
			ASSERT_FALSE(action.attack.shooting);
			const int distance = SpellTargetEvaluator::physicalTravelDistance(parent->getReachability(c.own), action.from);
			ASSERT_GE(distance, 0);
			ASSERT_LT(distance, 3);
			Exchange ordinary, charged;
			{
				auto branch = std::make_shared<HypotheticBattle>(c.environment.get(), parent);
				ASSERT_NO_FATAL_FAILURE(preview(branch, c.own->unitId(), c.enemy->unitId(), action.from, ordinary));
			}
			{
				auto branch = std::make_shared<HypotheticBattle>(c.environment.get(), parent);
				branch->setHeroOrderState(BattleSide::ATTACKER, prepared);
				ASSERT_NO_FATAL_FAILURE(preview(branch, c.own->unitId(), c.enemy->unitId(), action.from, charged));
			}
			EXPECT_FLOAT_EQ(charged.value, ordinary.value);
			EXPECT_EQ(charged.actorHealth, ordinary.actorHealth);
			EXPECT_EQ(charged.victimHealth, ordinary.victimHealth);
			if(!nativeFrom.isValid())
			{
				nativeFrom = action.from;
				expected = charged;
			}
		}
		EXPECT_EQ(snapshot(c, *parent), parentBefore);
		EXPECT_EQ(snapshot(c, *sibling), parentBefore);
		EXPECT_EQ(snapshot(c, *live), before);
	}
	ASSERT_TRUE(c.handler.battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE)));
	const auto round = live->battleGetRound();
	c.packets.attacks.clear();
	ASSERT_TRUE(c.handler.battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(c.own, c.enemy, nativeFrom, false)));
	ASSERT_EQ(c.packets.attacks.size(), 1u);
	EXPECT_EQ(c.packets.attacks.front().stackAttacking, c.own->unitId());
	ASSERT_EQ(c.packets.attacks.front().bsa.size(), 1u);
	EXPECT_EQ(c.packets.attacks.front().bsa.front().stackAttacked, c.enemy->unitId());
	EXPECT_FALSE(c.packets.attacks.front().counter());
	EXPECT_EQ(c.own->getAvailableHealth(), expected.actorHealth);
	EXPECT_EQ(c.enemy->getAvailableHealth(), expected.victimHealth);
	EXPECT_EQ(c.enemy->counterAttacks.available(), expected.victimCounters);
	EXPECT_EQ(live->battleGetRound(), round);
}
}
