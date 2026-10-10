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
#include "../../SpellPointTestUtils.h"

#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/NewHorizonsFrozen.h"
#include "../../../lib/battle/NewHorizonsPlague.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/mapObjects/army/CArmedInstance.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/networkPacks/StackLocation.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../server/queries/BattleQueries.h"
#include "../../../server/queries/QueriesProcessor.h"

class NewHorizonsFrozenServerTest : public BattleTestFixture
{
protected:
	bool computerPlayers = false;

	void configurePlayer(PlayerSettings & settings) const override
	{
		BattleTestFixture::configurePlayer(settings);
		if(computerPlayers)
			settings.connectedPlayerIDs.clear();
	}

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

	void configurePlagueCaster()
	{
		const SpellID spell(SpellID::decode(std::string(newHorizonsPlague::SPELL_ID)));
		ASSERT_TRUE(spell.hasValue());
		const SecondarySkill shadow(SecondarySkill::decode(newHorizonsPlague::SKILL));
		ASSERT_TRUE(shadow.hasValue());
		attackerSideHero->setSecSkillLevel(shadow, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 200, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(spell);
		setTestSpellPointTotal(attackerSideHero, 1000);
	}

	void castPlague(const CStack * target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID(SpellID::decode(std::string(newHorizonsPlague::SPELL_ID)));
		action.aimToUnit(target);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		ASSERT_TRUE(newHorizonsPlague::hasPlague(target));
	}

	std::shared_ptr<const Bonus> plagueStatus(const CStack * target) const
	{
		const SpellID spell(SpellID::decode(std::string(newHorizonsPlague::SPELL_ID)));
		const auto bonuses = target->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(spell)).And(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER)));
		return bonuses->empty() ? std::shared_ptr<const Bonus>() : bonuses->front();
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

TEST_F(NewHorizonsFrozenServerTest, AutomaticNormalForfeitureThawsOnceAndNextRoundAcceptsAnAction)
{
	startGame();
	startBattle();
	auto * leader = unit(BattleSide::ATTACKER, "core:angel", leftHex, 20);
	auto * target = unit(BattleSide::DEFENDER, "core:stoneGolem", rightHex, 500);
	beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), leader);
	const auto round = battle()->getRound();
	freeze(target);
	ASSERT_TRUE(newHorizonsFrozen::isFrozen(*target));
	ASSERT_FALSE(target->moved());
	const auto forfeitures = [this, target]()
	{
		return std::ranges::count_if(server.startedActions, [target](const auto & started)
		{
			return started.ba.stackNumber == target->unitId()
				&& started.ba.actionType == EActionType::NO_ACTION;
		});
	};

	// Answer real queue entries, never submit a player no-op for the Frozen
	// target. The scheduler must emit and finish its automatic forfeiture.
	for(int actions = 0; actions < 32 && forfeitures() == 0; ++actions)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_NE(active, target);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)));
	}
	ASSERT_FALSE(newHorizonsFrozen::isFrozen(*target));
	ASSERT_GE(battle()->getRound(), round);
	ASSERT_LE(battle()->getRound(), round + 1);
	EXPECT_EQ(forfeitures(), 1);
	EXPECT_FALSE(target->defended());
	EXPECT_EQ(target->frozenLastAppliedRound(), round);
	EXPECT_FALSE(newHorizonsFrozen::canApply(*target->acquireState(), round));
	EXPECT_TRUE(std::ranges::any_of(server.stackActivations, [target](const auto & activation)
	{
		return activation.stack == target->unitId()
			&& activation.reason == BattleUnitTurnReason::AUTOMATIC_ACTION;
	}));
	EXPECT_FALSE(std::ranges::any_of(server.stackActivations, [target](const auto & activation)
	{
		return activation.stack == target->unitId()
			&& activation.reason == BattleUnitTurnReason::MORALE;
	}));

	if(battle()->getRound() == round)
		endRound();
	ASSERT_EQ(battle()->getRound(), round + 1);
	for(int actions = 0; actions < 32 && battle()->battleActiveUnit() != target; ++actions)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_EQ(battle()->getRound(), round + 1);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)));
	}
	ASSERT_EQ(battle()->battleActiveUnit(), target);
	ASSERT_TRUE(target->canMove());
	EXPECT_EQ(forfeitures(), 1);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(target->unitSide()), BattleAction::makeDefend(target)));
	EXPECT_EQ(std::ranges::count_if(server.startedActions, [target](const auto & started)
	{
		return started.ba.stackNumber == target->unitId()
			&& started.ba.actionType == EActionType::DEFEND;
	}), 1);
	EXPECT_EQ(forfeitures(), 1);
	EXPECT_EQ(target->frozenLastAppliedRound(), round);
}

TEST_F(NewHorizonsFrozenServerTest, AutomaticTimeStopPassDoesNotThawAnUnspentFrozenMarker)
{
	startGame();
	startBattle();
	auto * leader = unit(BattleSide::ATTACKER, "core:angel", leftHex, 20);
	auto * target = unit(BattleSide::DEFENDER, "core:stoneGolem", rightHex, 500);
	beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), leader);
	const auto round = battle()->getRound();
	freeze(target);
	SetStackEffect stopped;
	stopped.battleID = BattleID(0);
	stopped.toAdd.emplace_back(target->unitId(), std::vector<Bonus>{
		Bonus(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID())});
	gameHandler->sendAndApply(stopped);
	ASSERT_TRUE(target->isTimeStopped());
	const auto passes = [this, target]()
	{
		return std::ranges::count_if(server.startedActions, [target](const auto & started)
		{
			return started.ba.stackNumber == target->unitId()
				&& started.ba.actionType == EActionType::NO_ACTION;
		});
	};
	for(int actions = 0; actions < 32 && passes() == 0; ++actions)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_NE(active, target);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)));
	}
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_EQ(target->frozenLastAppliedRound(), round);
	EXPECT_EQ(passes(), 1);
	EXPECT_GE(battle()->getRound(), round);
	EXPECT_LE(battle()->getRound(), round + 1);
}

TEST_F(NewHorizonsFrozenServerTest, AutomaticFrozenForfeitureTicksPlagueOnceBeforeThaw)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(configurePlagueCaster());
	startBattle();
	auto * leader = unit(BattleSide::ATTACKER, "core:angel", leftHex, 20);
	auto * target = unit(BattleSide::DEFENDER, "core:stoneGolem", BattleHex(12, 5).toInt(), 500);
	auto * reserve = unit(BattleSide::DEFENDER, "core:pikeman", BattleHex(12, 8).toInt(), 100);
	SetStackEffect slow;
	slow.battleID = BattleID(0);
	slow.toAdd.emplace_back(reserve->unitId(), std::vector<Bonus>{
		Bonus(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_FLAT, BonusSource::OTHER, -100, BonusSourceID())});
	gameHandler->sendAndApply(slow);
	ASSERT_GT(target->getInitiative(), reserve->getInitiative());
	beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), leader);
	ASSERT_NO_FATAL_FAILURE(castPlague(target));
	const auto status = plagueStatus(target);
	ASSERT_NE(status, nullptr);
	ASSERT_EQ(status->turnsRemain, 3);
	const auto before = target->getAvailableHealth();
	const auto expectedDamage = newHorizonsPlague::adjustedTickDamage(*battle(), BattleSide::ATTACKER,
		target, status->val);
	ASSERT_GT(expectedDamage, 0);
	ASSERT_LT(expectedDamage, before);
	const auto round = battle()->getRound();
	freeze(target);
	const auto forfeitures = [this, target]()
	{
		return std::ranges::count_if(server.startedActions, [target](const auto & started)
		{
			return started.ba.stackNumber == target->unitId()
				&& started.ba.actionType == EActionType::NO_ACTION;
		});
	};
	for(int actions = 0; actions < 32 && forfeitures() == 0; ++actions)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_NE(active, target);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)));
	}
	ASSERT_EQ(forfeitures(), 1);
	ASSERT_EQ(battle()->getRound(), round);
	EXPECT_EQ(before - target->getAvailableHealth(), expectedDamage);
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_EQ(target->frozenLastAppliedRound(), round);
	const auto after = plagueStatus(target);
	ASSERT_NE(after, nullptr);
	EXPECT_EQ(after->turnsRemain, 2);
	ASSERT_NE(after->parameters, nullptr);
	EXPECT_EQ(after->parameters->toCustom<JsonNode>()["lastProcessedRound"].Integer(), round);
	EXPECT_EQ(std::ranges::count_if(server.injuries, [target](const auto & injury)
	{
		return std::ranges::any_of(injury.stacks, [target](const auto & hit)
		{
			return hit.stackAttacked == target->unitId();
		});
	}), 1);
}

TEST_F(NewHorizonsFrozenServerTest, LethalPlagueDuringAutomaticFrozenForfeitureRetiresOnlyTheAfflictedUnit)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(configurePlagueCaster());
	startBattle();
	auto * leader = unit(BattleSide::ATTACKER, "core:angel", leftHex, 20);
	auto * target = unit(BattleSide::DEFENDER, "core:stoneGolem", BattleHex(12, 5).toInt(), 1);
	auto * reserve = unit(BattleSide::DEFENDER, "core:pikeman", BattleHex(12, 8).toInt(), 100);
	SetStackEffect slow;
	slow.battleID = BattleID(0);
	slow.toAdd.emplace_back(reserve->unitId(), std::vector<Bonus>{
		Bonus(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_FLAT, BonusSource::OTHER, -100, BonusSourceID())});
	gameHandler->sendAndApply(slow);
	ASSERT_GT(target->getInitiative(), reserve->getInitiative());
	beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), leader);
	ASSERT_NO_FATAL_FAILURE(castPlague(target));
	const auto status = plagueStatus(target);
	ASSERT_NE(status, nullptr);
	const auto availableHealth = target->getAvailableHealth();
	ASSERT_GT(newHorizonsPlague::adjustedTickDamage(*battle(), BattleSide::ATTACKER, target, status->val),
		availableHealth);
	const auto targetID = target->unitId();
	const auto round = battle()->getRound();
	freeze(target);
	const auto forfeitures = [this, targetID]()
	{
		return std::ranges::count_if(server.startedActions, [targetID](const auto & started)
		{
			return started.ba.stackNumber == targetID && started.ba.actionType == EActionType::NO_ACTION;
		});
	};
	for(int actions = 0; actions < 32 && forfeitures() == 0; ++actions)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_NE(active->unitId(), targetID);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)));
	}
	ASSERT_EQ(forfeitures(), 1);
	// Do not retain/dereference the old target pointer across lethal cleanup.
	const auto * current = gameState()->getBattle(BattleID(0));
	ASSERT_NE(current, nullptr);
	EXPECT_EQ(current->getRound(), round);
	EXPECT_FALSE(current->battleIsFinished().has_value());
	if(const auto * remains = current->battleGetStackByID(targetID, false))
	{
		EXPECT_FALSE(remains->alive());
		EXPECT_FALSE(newHorizonsPlague::hasPlague(remains));
		EXPECT_EQ(remains->frozenLastAppliedRound(), round);
	}
	int64_t damage = 0;
	int64_t killed = 0;
	int hits = 0;
	for(const auto & injury : server.injuries)
		for(const auto & hit : injury.stacks)
			if(hit.stackAttacked == targetID)
			{
				damage += hit.damageAmount;
				killed += hit.killedAmount;
				++hits;
			}
	EXPECT_EQ(damage, availableHealth);
	EXPECT_EQ(killed, 1);
	EXPECT_EQ(hits, 1);
	EXPECT_TRUE(leader->alive());
	EXPECT_TRUE(reserve->alive());
}

TEST_F(NewHorizonsFrozenServerTest, LethalAutomaticFrozenPlagueSafelyFinalizesComputerBattle)
{
	computerPlayers = true;
	startGame();
	ASSERT_FALSE(gameState()->getPlayerState(PlayerColor(0))->isHuman());
	ASSERT_FALSE(gameState()->getPlayerState(PlayerColor(1))->isHuman());
	ASSERT_NO_FATAL_FAILURE(configurePlagueCaster());
	const auto golem = creatureByName("core:stoneGolem");
	ASSERT_TRUE(gameHandler->changeStackType(StackLocation(defenderSideHero->id, SlotID(0)), golem.toCreature()));
	ASSERT_TRUE(gameHandler->changeStackCount(StackLocation(defenderSideHero->id, SlotID(0)), 1,
		ChangeValueMode::ABSOLUTE));
	startBattle();
	const auto battleID = battle()->getBattleID();
	const auto defenders = battle()->battleGetStacksIf([](const CStack * stack)
	{
		return stack->unitSide() == BattleSide::DEFENDER && stack->alive();
	});
	ASSERT_EQ(defenders.size(), 1u);
	const auto * target = defenders.front();
	ASSERT_EQ(target->creatureId(), golem);
	const auto targetID = target->unitId();
	const auto availableHealth = target->getAvailableHealth();
	auto * leader = unit(BattleSide::ATTACKER, "core:angel", leftHex, 20);
	const auto query = std::make_shared<CBattleQuery>(gameHandler.get(), battle());
	gameHandler->queries->addQuery(query);
	beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), leader);
	ASSERT_NO_FATAL_FAILURE(castPlague(target));
	const auto status = plagueStatus(target);
	ASSERT_NE(status, nullptr);
	ASSERT_GT(newHorizonsPlague::adjustedTickDamage(*battle(), BattleSide::ATTACKER, target, status->val),
		availableHealth);
	freeze(target);

	for(int actions = 0; actions < 32; ++actions)
	{
		const auto * current = gameState()->getBattle(battleID);
		if(!current)
			break;
		const auto * active = current->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_NE(active->unitId(), targetID);
		const auto player = current->sideToPlayer(active->unitSide());
		const auto action = BattleAction::makeDefend(active);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battleID, player, action));
	}
	// The actual result/query continuation deletes BattleInfo synchronously.
	// All following observations use retained packets/IDs, never old pointers.
	ASSERT_EQ(gameState()->getBattle(battleID), nullptr);
	ASSERT_TRUE(query->result.has_value());
	EXPECT_EQ(query->result->winner, BattleSide::ATTACKER);
	EXPECT_EQ(std::ranges::count_if(server.startedActions, [targetID](const auto & started)
	{
		return started.ba.stackNumber == targetID && started.ba.actionType == EActionType::NO_ACTION;
	}), 1);
	int64_t damage = 0;
	int64_t killed = 0;
	int hits = 0;
	for(const auto & injury : server.injuries)
		for(const auto & hit : injury.stacks)
			if(hit.stackAttacked == targetID)
			{
				damage += hit.damageAmount;
				killed += hit.killedAmount;
				++hits;
			}
	EXPECT_EQ(damage, availableHealth);
	EXPECT_EQ(killed, 1);
	EXPECT_EQ(hits, 1);
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
