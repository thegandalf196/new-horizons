/*
 * NewHorizonsDemonicGatingTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/IdentifierStorage.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/networkPacks/StackLocation.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"

namespace
{
class NewHorizonsDemonicGatingTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
		startGame();
		const auto imp = creatureByName("core:imp");
		ASSERT_TRUE(gameHandler->changeStackType(StackLocation(attackerSideHero->id, SlotID(0)), imp.toCreature()));
		attackerSideHero->setSecSkillLevel(
			SecondarySkill(SecondarySkill::decode("new-horizons:demonicGating")),
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setDemonicReserve({{imp, 12}});
		startBattle();
		beginCombat();
	}

	BattleHex legalGateHex(const battle::Unit * source) const
	{
		const auto accessibility = battle()->getAccessibility();
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			BattleHex candidate(index);
			if(candidate.isAvailable() && BattleHex::getDistance(source->getPosition(), candidate) <= 3
				&& accessibility.accessible(candidate, false, source->unitSide()))
				return candidate;
		}
		return BattleHex();
	}

	void grantGatingPerk(const std::string & perkId, int rank = MasteryLevel::BASIC)
	{
		attackerSideHero->setSecSkillLevel(
			SecondarySkill(SecondarySkill::decode("new-horizons:demonicGating")),
			rank, ChangeValueMode::ABSOLUTE);
		auto & state = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
		state.selected.clear();
		state.select("new-horizons:demonicGating", perkId, rank);
		ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:demonicGating", perkId));
	}

	void grantChainGate(bool swift = false)
	{
		grantGatingPerk("new-horizons:demonicGating.chainGate", MasteryLevel::ADVANCED);
		if(swift)
		{
			auto & state = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
			state.select("new-horizons:demonicGating",
				"new-horizons:demonicGating.swiftGate", MasteryLevel::ADVANCED);
			ASSERT_TRUE(attackerSideHero->hasActivePerk(
				"new-horizons:demonicGating", "new-horizons:demonicGating.swiftGate"));
		}
	}

	void attachCombatScript(CStack * unit, const std::string & scriptName, int value)
	{
		const auto script = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "script", scriptName);
		ASSERT_TRUE(script.has_value());
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::COMBAT_EVENT_TRIGGER, BonusSource::OTHER, value, BonusSourceID(), BonusSubtypeID(ScriptID(*script))));
	}

	BattleHex gateHexAtDistance(const battle::Unit * source, int minimum, int maximum) const
	{
		const auto accessibility = battle()->getAccessibility();
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			BattleHex candidate(index);
			const auto distance = BattleHex::getDistance(source->getPosition(), candidate);
			if(candidate.isAvailable() && distance >= minimum && distance <= maximum
				&& accessibility.accessible(candidate, false, source->unitSide()))
				return candidate;
		}
		return BattleHex();
	}

	std::pair<BattleHex, BattleHex> mobileMoveAndGate(const battle::Unit * source,
		int minimumPath, int maximumPath, bool requireBeyondOriginalRange = false) const
	{
		const auto movementAccessibility = battle()->getAccessibility(source);
		const auto gateAccessibility = battle()->getAccessibility();
		for(int moveIndex = 0; moveIndex < GameConstants::BFIELD_SIZE; ++moveIndex)
		{
			const BattleHex movement(moveIndex);
			if(!movement.isAvailable() || !movementAccessibility.accessible(movement, source))
				continue;
			const auto [path, distance] = battle()->getPath(source->getPosition(), movement, source);
			if(path.empty() || distance < minimumPath || distance > maximumPath)
				continue;
			for(int gateIndex = 0; gateIndex < GameConstants::BFIELD_SIZE; ++gateIndex)
			{
				const BattleHex gate(gateIndex);
				if(!gate.isAvailable() || gate == movement || gate == source->getPosition()
					|| BattleHex::getDistance(movement, gate) > 3
					|| (requireBeyondOriginalRange && BattleHex::getDistance(source->getPosition(), gate) <= 3)
					|| !gateAccessibility.accessible(gate, false, source->unitSide())
					|| battle()->battleGetUnitByPos(gate, true)
					|| !battle()->battleGetAllObstaclesOnPos(gate, false).empty())
					continue;
				return {movement, gate};
			}
		}
		return {};
	}

	std::pair<BattleHex, BattleHex> adjacentGateAndEnemyHexes(const battle::Unit * source) const
	{
		const auto accessibility = battle()->getAccessibility();
		for(int gateIndex = 0; gateIndex < GameConstants::BFIELD_SIZE; ++gateIndex)
		{
			BattleHex gate(gateIndex);
			if(!gate.isAvailable() || BattleHex::getDistance(source->getPosition(), gate) > 3
				|| !accessibility.accessible(gate, false, source->unitSide()))
				continue;
			for(int enemyIndex = 0; enemyIndex < GameConstants::BFIELD_SIZE; ++enemyIndex)
			{
				BattleHex enemy(enemyIndex);
				if(enemy.isAvailable() && BattleHex::getDistance(gate, enemy) == 1
					&& accessibility.accessible(enemy, false, BattleSide::DEFENDER))
					return {gate, enemy};
			}
		}
		return {BattleHex(), BattleHex()};
	}

	BattleHex adjacentEnemyHex(const battle::Unit * source) const
	{
		const auto accessibility = battle()->getAccessibility();
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			const BattleHex candidate(index);
			if(candidate.isAvailable()
				&& BattleHex::getDistance(source->getPosition(), candidate) == 1
				&& accessibility.accessible(candidate, false, BattleSide::DEFENDER)
				&& !battle()->battleGetUnitByPos(candidate, true)
				&& battle()->battleGetAllObstaclesOnPos(candidate, false).empty())
				return candidate;
		}
		return BattleHex();
	}
};
}

TEST_F(NewHorizonsDemonicGatingTest, CommitsOwnedReserveAndArrivesAtNextRound)
{
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	ASSERT_EQ(active->unitSide(), BattleSide::ATTACKER);
	const BattleHex destination = legalGateHex(active);
	ASSERT_TRUE(destination.isAvailable());

	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = creatureByName("core:imp");
	action.aimToHex(destination);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	ASSERT_TRUE(battle()->getSide(BattleSide::ATTACKER).demonicReserve.empty());
	ASSERT_EQ(battle()->getSide(BattleSide::ATTACKER).pendingDemonicGates.size(), 1u);

	endRound();
	const auto gated = battle()->battleGetStacksIf([](const CStack * stack)
	{
		return stack->unitSlot() == SlotID::SUMMONED_SLOT_PLACEHOLDER
			&& stack->creatureId() == creatureByName("core:imp") && stack->getCount() == 12;
	});
	ASSERT_EQ(gated.size(), 1u);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).pendingDemonicGates.empty());
	ASSERT_EQ(battle()->getSide(BattleSide::ATTACKER).gatedDemonicStacks.size(), 1u);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).gatedDemonicStacks.front().unitId, gated.front()->unitId());
}

TEST_F(NewHorizonsDemonicGatingTest, RejectsUnavailableAndOutOfRangeSelectionsWithoutSpendingTurn)
{
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = creatureByName("core:devil");
	action.aimToHex(BattleHex(rightHex));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_FALSE(active->moved());
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).demonicReserve.at(creatureByName("core:imp")), 12);
}

TEST_F(NewHorizonsDemonicGatingTest, WideGateAuthoritativelyExtendsPlacementRangeToFive)
{
	grantGatingPerk("new-horizons:demonicGating.wideGate");
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const BattleHex destination = gateHexAtDistance(active, 4, 5);
	ASSERT_TRUE(destination.isAvailable());

	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = creatureByName("core:imp");
	action.aimToHex(destination);
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
}

TEST_F(NewHorizonsDemonicGatingTest, GateBeyondThreeHexesIsRejectedWithoutWideGate)
{
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const BattleHex destination = gateHexAtDistance(active, 4, 5);
	ASSERT_TRUE(destination.isAvailable());

	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = creatureByName("core:imp");
	action.aimToHex(destination);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_FALSE(active->moved());
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).demonicReserve.at(creatureByName("core:imp")), 12);
}

TEST_F(NewHorizonsDemonicGatingTest, SwiftGateArrivesBeforeTheRoundCounterAdvances)
{
	grantGatingPerk("new-horizons:demonicGating.swiftGate");
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const BattleHex destination = legalGateHex(active);
	ASSERT_TRUE(destination.isAvailable());
	const int32_t openingRound = battle()->getRound();
	server.unitAdditionRounds.clear();

	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = creatureByName("core:imp");
	action.aimToHex(destination);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	endRound();

	ASSERT_EQ(server.unitAdditionRounds.size(), 1u);
	EXPECT_EQ(server.unitAdditionRounds.front(), openingRound);
	EXPECT_EQ(battle()->getRound(), openingRound + 1);
}

TEST_F(NewHorizonsDemonicGatingTest, HellfireArrivalDealsFifteenPercentAggregateHealthToAdjacentEnemies)
{
	grantGatingPerk("new-horizons:demonicGating.hellfireArrival");
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const auto [destination, enemyHex] = adjacentGateAndEnemyHexes(active);
	ASSERT_TRUE(destination.isAvailable());
	ASSERT_TRUE(enemyHex.isAvailable());
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:imp"), enemyHex, 100);
	ASSERT_NE(enemy, nullptr);
	const uint32_t enemyId = enemy->unitId();
	const int64_t healthBefore = enemy->getAvailableHealth();
	server.injuries.clear();
	active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);

	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = creatureByName("core:imp");
	action.aimToHex(destination);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	endRound();

	const auto gated = battle()->battleGetStacksIf([](const CStack * stack)
	{
		return stack->unitSlot() == SlotID::SUMMONED_SLOT_PLACEHOLDER
			&& stack->unitSide() == BattleSide::ATTACKER;
	});
	ASSERT_EQ(gated.size(), 1u);
	const auto * damagedEnemy = battle()->battleGetStackByID(enemyId, false);
	ASSERT_NE(damagedEnemy, nullptr);
	const int64_t expected = gated.front()->getAvailableHealth() * 15 / 100;
	EXPECT_EQ(healthBefore - damagedEnemy->getAvailableHealth(), expected);
	ASSERT_EQ(server.injuries.size(), 1u);
	ASSERT_EQ(server.injuries.front().stacks.size(), 1u);
	EXPECT_EQ(server.injuries.front().stacks.front().damageAmount, expected);
}

TEST_F(NewHorizonsDemonicGatingTest, HellfireLethalKillByGatedUnitArmsChainGate)
{
	grantChainGate();
	auto & state = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
	state.select("new-horizons:demonicGating",
		"new-horizons:demonicGating.hellfireArrival", MasteryLevel::ADVANCED);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(
		"new-horizons:demonicGating", "new-horizons:demonicGating.hellfireArrival"));

	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const auto [destination, enemyHex] = adjacentGateAndEnemyHexes(active);
	ASSERT_TRUE(destination.isAvailable());
	ASSERT_TRUE(enemyHex.isAvailable());
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:imp"), enemyHex, 1);
	ASSERT_NE(enemy, nullptr);
	server.injuries.clear();

	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = creatureByName("core:imp");
	action.aimToHex(destination);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	endRound();

	const auto & gated = battle()->getSide(BattleSide::ATTACKER).gatedDemonicStacks;
	ASSERT_EQ(gated.size(), 1u);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).chainGateArmed);
	EXPECT_TRUE(std::ranges::any_of(server.injuries, [&gated](const auto & injury)
	{
		return std::ranges::any_of(injury.stacks, [&gated](const auto & hit)
		{
			return hit.attackerID == gated.front().unitId && hit.killed()
				&& !hit.cloneKilled() && !hit.willRebirth();
		});
	}));
}

TEST_F(NewHorizonsDemonicGatingTest, ReinforcedGateAddsTwentyPercentTemporaryHealthConsumedFirst)
{
	grantGatingPerk("new-horizons:demonicGating.reinforcedGate");
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const BattleHex destination = legalGateHex(active);
	ASSERT_TRUE(destination.isAvailable());

	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = creatureByName("core:imp");
	action.aimToHex(destination);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	endRound();

	const auto gated = battle()->battleGetStacksIf([](const CStack * stack)
	{
		return stack->unitSlot() == SlotID::SUMMONED_SLOT_PLACEHOLDER
			&& stack->unitSide() == BattleSide::ATTACKER;
	});
	ASSERT_EQ(gated.size(), 1u);
	const int64_t creatureHealth = gated.front()->getTotalHealth();
	const int64_t temporaryHealth = creatureHealth * 20 / 100;
	EXPECT_EQ(gated.front()->health.getTemporaryHitPoints(), temporaryHealth);
	EXPECT_EQ(gated.front()->getAvailableHealth(), creatureHealth + temporaryHealth);

	auto state = gated.front()->acquireState();
	int64_t damage = temporaryHealth;
	state->damage(damage);
	EXPECT_EQ(damage, temporaryHealth);
	EXPECT_EQ(state->health.getTemporaryHitPoints(), 0);
	EXPECT_EQ(state->getAvailableHealth(), creatureHealth);
	EXPECT_EQ(state->getCount(), gated.front()->getCount());
}

TEST_F(NewHorizonsDemonicGatingTest, InfernalBeaconAddsTwoFlatInitiativeBesideInfernoAlly)
{
	grantGatingPerk("new-horizons:demonicGating.infernalBeacon", MasteryLevel::ADVANCED);
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const int32_t ordinaryInitiative = active->getInitiative();
	const BattleHex destination = gateHexAtDistance(active, 1, 1);
	ASSERT_TRUE(destination.isAvailable());

	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = creatureByName("core:imp");
	action.aimToHex(destination);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	endRound();

	const auto gated = battle()->battleGetStacksIf([](const CStack * stack)
	{
		return stack->unitSlot() == SlotID::SUMMONED_SLOT_PLACEHOLDER
			&& stack->unitSide() == BattleSide::ATTACKER;
	});
	ASSERT_EQ(gated.size(), 1u);
	EXPECT_EQ(gated.front()->getInitiative(), ordinaryInitiative + 2);
	const auto bonus = gated.front()->getFirstBonus(Selector::type()(BonusType::STACKS_INITIATIVE_FLAT));
	ASSERT_NE(bonus, nullptr);
	EXPECT_EQ(bonus->turnsRemain, 1);
}

TEST_F(NewHorizonsDemonicGatingTest, ReserveDisciplineFloorsNegativeArrivalMoraleAtZero)
{
	grantGatingPerk("new-horizons:demonicGating.reserveDiscipline", MasteryLevel::ADVANCED);
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const BattleHex destination = legalGateHex(active);
	ASSERT_TRUE(destination.isAvailable());

	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = creatureByName("core:imp");
	action.aimToHex(destination);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	endRound();

	const auto gated = battle()->battleGetStacksIf([](const CStack * stack)
	{
		return stack->unitSlot() == SlotID::SUMMONED_SLOT_PLACEHOLDER
			&& stack->unitSide() == BattleSide::ATTACKER;
	});
	ASSERT_EQ(gated.size(), 1u);
	auto * gatedStack = const_cast<CStack *>(gated.front());
	gatedStack->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::ONE_BATTLE, BonusType::MORALE, BonusSource::OTHER, -3, BonusSourceID()));
	EXPECT_EQ(gatedStack->moraleVal(), 0);
	const auto floor = gatedStack->getFirstBonus(Selector::type()(BonusType::MINIMUM_MORALE));
	ASSERT_NE(floor, nullptr);
	EXPECT_EQ(floor->turnsRemain, 1);
}

TEST_F(NewHorizonsDemonicGatingTest, MobileGateMovesWithinHalfSpeedThenCommitsFromNewPosition)
{
	grantGatingPerk("new-horizons:demonicGating.mobileGate", MasteryLevel::ADVANCED);
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	ASSERT_EQ(active->getMovementRange(0) % 2, 1u);
	const int movementLimit = static_cast<int>(active->getMovementRange(0) / 2);
	ASSERT_GT(movementLimit, 0);
	const auto [movement, gate] = mobileMoveAndGate(active, 1, movementLimit, true);
	ASSERT_TRUE(movement.isAvailable());
	ASSERT_TRUE(gate.isAvailable());

	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = creatureByName("core:imp");
	action.aimToHex(movement);
	action.aimToHex(gate);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));

	const auto * moved = battle()->battleGetStackByID(active->unitId(), false);
	ASSERT_NE(moved, nullptr);
	EXPECT_EQ(moved->getPosition(), movement);
	const auto & side = battle()->getSide(BattleSide::ATTACKER);
	EXPECT_TRUE(side.demonicReserve.empty());
	ASSERT_EQ(side.pendingDemonicGates.size(), 1u);
	EXPECT_EQ(side.pendingDemonicGates.front().position, gate);
}

TEST_F(NewHorizonsDemonicGatingTest, MobileGateRejectsMovementBeyondFlooredHalfSpeedAtomically)
{
	grantGatingPerk("new-horizons:demonicGating.mobileGate", MasteryLevel::ADVANCED);
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const auto originalPosition = active->getPosition();
	const int movementLimit = static_cast<int>(active->getMovementRange(0) / 2);
	const auto [movement, gate] = mobileMoveAndGate(active, movementLimit + 1,
		static_cast<int>(active->getMovementRange(0)));
	ASSERT_TRUE(movement.isAvailable());
	ASSERT_TRUE(gate.isAvailable());

	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = creatureByName("core:imp");
	action.aimToHex(movement);
	action.aimToHex(gate);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));

	const auto * unchanged = battle()->battleGetStackByID(active->unitId(), false);
	ASSERT_NE(unchanged, nullptr);
	EXPECT_EQ(unchanged->getPosition(), originalPosition);
	const auto & side = battle()->getSide(BattleSide::ATTACKER);
	EXPECT_EQ(side.demonicReserve.at(creatureByName("core:imp")), 12);
	EXPECT_TRUE(side.pendingDemonicGates.empty());
}

TEST_F(NewHorizonsDemonicGatingTest, MobileGateUsesExactlyHalfOfEvenMovementRange)
{
	grantGatingPerk("new-horizons:demonicGating.mobileGate", MasteryLevel::ADVANCED);
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	if(active->getMovementRange(0) % 2 != 0)
	{
		Bonus movement;
		movement.type = BonusType::STACKS_MOVEMENT_RANGE;
		movement.val = 1;
		movement.duration = BonusDuration::ONE_BATTLE;
		SetStackEffect effect;
		effect.battleID = BattleID(0);
		effect.toAdd.emplace_back(active->unitId(), std::vector<Bonus>{movement});
		gameHandler->sendAndApply(effect);
	}
	ASSERT_EQ(active->getMovementRange(0) % 2, 0u);
	const int movementLimit = static_cast<int>(active->getMovementRange(0) / 2);
	const auto [movement, gate] = mobileMoveAndGate(active, movementLimit, movementLimit);
	ASSERT_TRUE(movement.isAvailable());
	ASSERT_TRUE(gate.isAvailable());

	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = creatureByName("core:imp");
	action.aimToHex(movement);
	action.aimToHex(gate);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(battle()->battleGetStackByID(active->unitId(), false)->getPosition(), movement);
}

TEST_F(NewHorizonsDemonicGatingTest, MobileGateRejectsGateOnTheMovementHexAtomically)
{
	grantGatingPerk("new-horizons:demonicGating.mobileGate", MasteryLevel::ADVANCED);
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const auto originalPosition = active->getPosition();
	const int movementLimit = static_cast<int>(active->getMovementRange(0) / 2);
	const auto selection = mobileMoveAndGate(active, 1, movementLimit);
	const auto movement = selection.first;
	ASSERT_TRUE(movement.isAvailable());

	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = creatureByName("core:imp");
	action.aimToHex(movement);
	action.aimToHex(movement);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));

	EXPECT_EQ(battle()->battleGetStackByID(active->unitId(), false)->getPosition(), originalPosition);
	const auto & side = battle()->getSide(BattleSide::ATTACKER);
	EXPECT_EQ(side.demonicReserve.at(creatureByName("core:imp")), 12);
	EXPECT_TRUE(side.pendingDemonicGates.empty());
}

TEST_F(NewHorizonsDemonicGatingTest, MobileGateRejectsDoubleWideTailOverlappingFutureSourceAtomically)
{
	grantGatingPerk("new-horizons:demonicGating.mobileGate", MasteryLevel::ADVANCED);
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const auto originalPosition = active->getPosition();
	const auto hellHound = creatureByName("core:hellHound");
	auto & side = battle()->getSide(BattleSide::ATTACKER);
	side.demonicReserve = {{hellHound, 4}};

	const auto movementAccessibility = battle()->getAccessibility(active);
	const auto gateAccessibility = battle()->getAccessibility();
	const int movementLimit = static_cast<int>(active->getMovementRange(0) / 2);
	BattleHex movement;
	BattleHex gate;
	for(int index = 0; index < GameConstants::BFIELD_SIZE && !gate.isAvailable(); ++index)
	{
		const BattleHex candidate(index);
		const BattleHex candidateGate(candidate.toInt() + 1);
		if(!candidate.isAvailable() || !candidateGate.isAvailable()
			|| BattleHex::getDistance(candidate, candidateGate) != 1
			|| !movementAccessibility.accessible(candidate, active)
			|| !gateAccessibility.accessible(candidateGate, true, BattleSide::ATTACKER))
			continue;
		const auto [path, distance] = battle()->getPath(originalPosition, candidate, active);
		if(!path.empty() && distance > 0 && distance <= movementLimit)
		{
			movement = candidate;
			gate = candidateGate;
		}
	}
	ASSERT_TRUE(movement.isAvailable());
	ASSERT_TRUE(gate.isAvailable());
	ASSERT_EQ(battle::Unit::occupiedHex(gate, true, BattleSide::ATTACKER), movement);

	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = hellHound;
	action.aimToHex(movement);
	action.aimToHex(gate);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));

	EXPECT_EQ(battle()->battleGetStackByID(active->unitId(), false)->getPosition(), originalPosition);
	EXPECT_EQ(side.demonicReserve.at(hellHound), 4);
	EXPECT_TRUE(side.pendingDemonicGates.empty());
}

TEST_F(NewHorizonsDemonicGatingTest, ChainGateTokenIsCappedPerSide)
{
	grantChainGate();
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).chainGateArmed);
	battle()->armChainGate(BattleSide::ATTACKER);
	battle()->armChainGate(BattleSide::ATTACKER);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).chainGateArmed);
	EXPECT_TRUE(battle()->consumeChainGate(BattleSide::ATTACKER));
	EXPECT_FALSE(battle()->consumeChainGate(BattleSide::ATTACKER));
}

TEST_F(NewHorizonsDemonicGatingTest, InvalidChainGateKeepsArmedToken)
{
	grantChainGate();
	battle()->armChainGate(BattleSide::ATTACKER);
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const BattleHex destination = gateHexAtDistance(active, 4, 5);
	ASSERT_TRUE(destination.isAvailable());

	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = creatureByName("core:imp");
	action.aimToHex(destination);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).chainGateArmed);
}

TEST_F(NewHorizonsDemonicGatingTest, AcceptedChainGateConsumesTokenAndArrivesAtCurrentRoundBoundary)
{
	grantChainGate();
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const auto destination = legalGateHex(active);
	ASSERT_TRUE(destination.isAvailable());
	const int32_t openingRound = battle()->getRound();
	battle()->armChainGate(BattleSide::ATTACKER);

	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = creatureByName("core:imp");
	action.aimToHex(destination);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));

	const auto & side = battle()->getSide(BattleSide::ATTACKER);
	ASSERT_FALSE(side.chainGateArmed);
	ASSERT_EQ(side.pendingDemonicGates.size(), 1u);
	EXPECT_EQ(side.pendingDemonicGates.front().arrivalRound, openingRound);
	EXPECT_TRUE(side.pendingDemonicGates.front().chainGateAccelerated);
	server.unitAdditionRounds.clear();
	endRound();
	ASSERT_EQ(server.unitAdditionRounds.size(), 1u);
	EXPECT_EQ(server.unitAdditionRounds.front(), openingRound);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).pendingDemonicGates.empty());
}

TEST_F(NewHorizonsDemonicGatingTest, SwiftChainGateConsumesTokenWithoutEarlierTiming)
{
	grantChainGate(true);
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const auto destination = legalGateHex(active);
	ASSERT_TRUE(destination.isAvailable());
	const int32_t openingRound = battle()->getRound();
	battle()->armChainGate(BattleSide::ATTACKER);

	BattleAction action;
	action.actionType = EActionType::DEMONIC_GATING;
	action.side = BattleSide::ATTACKER;
	action.stackNumber = active->unitId();
	action.gatingCreature = creatureByName("core:imp");
	action.aimToHex(destination);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));

	const auto & side = battle()->getSide(BattleSide::ATTACKER);
	ASSERT_FALSE(side.chainGateArmed);
	ASSERT_EQ(side.pendingDemonicGates.size(), 1u);
	EXPECT_EQ(side.pendingDemonicGates.front().arrivalRound, openingRound + 1);
	EXPECT_FALSE(side.pendingDemonicGates.front().chainGateAccelerated);
	server.unitAdditionRounds.clear();
	endRound();
	ASSERT_EQ(server.unitAdditionRounds.size(), 1u);
	EXPECT_EQ(server.unitAdditionRounds.front(), openingRound);
}

TEST_F(NewHorizonsDemonicGatingTest, GatedEnemyKillArmsChainGateThroughBattleAttack)
{
	grantChainGate();
	const auto * source = battle()->battleActiveUnit();
	ASSERT_NE(source, nullptr);
	const auto [destination, enemyHex] = adjacentGateAndEnemyHexes(source);
	ASSERT_TRUE(destination.isAvailable());
	ASSERT_TRUE(enemyHex.isAvailable());
	const auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), enemyHex, 1);
	ASSERT_NE(enemy, nullptr);
	blockRetaliation(const_cast<CStack *>(enemy));

	BattleAction gate;
	gate.actionType = EActionType::DEMONIC_GATING;
	gate.side = BattleSide::ATTACKER;
	gate.stackNumber = source->unitId();
	gate.gatingCreature = creatureByName("core:imp");
	gate.aimToHex(destination);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), gate));
	endRound();

	const auto & gatedState = battle()->getSide(BattleSide::ATTACKER).gatedDemonicStacks;
	ASSERT_EQ(gatedState.size(), 1u);
	const auto * gated = battle()->battleGetStackByID(gatedState.front().unitId, false);
	ASSERT_NE(gated, nullptr);
	forceMaximumDamage(const_cast<CStack *>(gated));
	server.attacks.clear();
	ASSERT_TRUE(attack(gated, enemyHex));
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).chainGateArmed);
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_TRUE(std::ranges::any_of(server.attacks, [](const auto & attack)
	{
		return attack.chainGateTriggered;
	}));
}

TEST_F(NewHorizonsDemonicGatingTest, NonGatedEnemyKillDoesNotArmChainGate)
{
	grantChainGate();
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const auto * attacker = battle()->battleGetStackByID(active->unitId(), false);
	ASSERT_NE(attacker, nullptr);
	const BattleHex enemyHex = adjacentEnemyHex(active);
	ASSERT_TRUE(enemyHex.isAvailable());
	const auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), enemyHex, 1);
	ASSERT_NE(enemy, nullptr);
	blockRetaliation(const_cast<CStack *>(enemy));
	forceMaximumDamage(const_cast<CStack *>(attacker));
	server.attacks.clear();

	ASSERT_TRUE(attack(attacker, enemyHex));
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).chainGateArmed);
	EXPECT_FALSE(std::ranges::any_of(server.attacks, [](const auto & attack)
	{
		return attack.chainGateTriggered;
	}));
}

TEST_F(NewHorizonsDemonicGatingTest, FireShieldLethalKillByGatedUnitArmsChainGate)
{
	grantChainGate();
	const auto * source = battle()->battleActiveUnit();
	ASSERT_NE(source, nullptr);
	const auto [destination, enemyHex] = adjacentGateAndEnemyHexes(source);
	ASSERT_TRUE(destination.isAvailable());
	ASSERT_TRUE(enemyHex.isAvailable());

	BattleAction gate;
	gate.actionType = EActionType::DEMONIC_GATING;
	gate.side = BattleSide::ATTACKER;
	gate.stackNumber = source->unitId();
	gate.gatingCreature = creatureByName("core:imp");
	gate.aimToHex(destination);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), gate));
	endRound();

	const auto & gatedState = battle()->getSide(BattleSide::ATTACKER).gatedDemonicStacks;
	ASSERT_EQ(gatedState.size(), 1u);
	auto * gated = const_cast<CStack *>(battle()->battleGetStackByID(gatedState.front().unitId, false));
	ASSERT_NE(gated, nullptr);
	const auto reflectedEnemyHex = adjacentEnemyHex(gated);
	ASSERT_TRUE(reflectedEnemyHex.isAvailable());
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:imp"), reflectedEnemyHex, 1);
	ASSERT_NE(enemy, nullptr);
	blockRetaliation(enemy);
	forceMaximumDamage(enemy);
	// The very high test-only value makes a one-imp reflected hit lethal while exercising the
	// actual combat event script and its source Unit argument.
	attachCombatScript(gated, "fireShield", 1000);
	server.injuries.clear();

	ASSERT_TRUE(attack(enemy, gated->getPosition()));
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).chainGateArmed);
	EXPECT_TRUE(std::ranges::any_of(server.injuries, [gatedState](const auto & injury)
	{
		return std::ranges::any_of(injury.stacks, [gatedState](const auto & hit)
		{
			return hit.attackerID == gatedState.front().unitId && hit.killed()
				&& !hit.cloneKilled() && !hit.willRebirth();
		});
	}));
}

TEST_F(NewHorizonsDemonicGatingTest, HeroSpellDamageRemainsUnattributed)
{
	grantChainGate();
	const auto * source = battle()->battleActiveUnit();
	ASSERT_NE(source, nullptr);
	const auto destination = legalGateHex(source);
	ASSERT_TRUE(destination.isAvailable());

	BattleAction gate;
	gate.actionType = EActionType::DEMONIC_GATING;
	gate.side = BattleSide::ATTACKER;
	gate.stackNumber = source->unitId();
	gate.gatingCreature = creatureByName("core:imp");
	gate.aimToHex(destination);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), gate));
	endRound();

	const auto & gatedState = battle()->getSide(BattleSide::ATTACKER).gatedDemonicStacks;
	ASSERT_EQ(gatedState.size(), 1u);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:imp"), BattleHex(rightHex), 1);
	ASSERT_NE(enemy, nullptr);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
	attackerSideHero->mana = 9999;
	server.injuries.clear();

	ASSERT_TRUE(castOn(attackerSideHero, SpellID::MAGIC_ARROW, enemy));
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).chainGateArmed);
	EXPECT_TRUE(std::ranges::any_of(server.injuries, [enemy](const auto & injury)
	{
		return std::ranges::any_of(injury.stacks, [enemy](const auto & hit)
		{
			return hit.stackAttacked == enemy->unitId() && hit.killed() && hit.attackerID == uint32_t(-1);
		});
	}));
}

TEST_F(NewHorizonsDemonicGatingTest, GatedCreatureSpellLethalKillArmsChainGate)
{
	grantChainGate();
	const auto * source = battle()->battleActiveUnit();
	ASSERT_NE(source, nullptr);
	const auto destination = legalGateHex(source);
	ASSERT_TRUE(destination.isAvailable());

	BattleAction gate;
	gate.actionType = EActionType::DEMONIC_GATING;
	gate.side = BattleSide::ATTACKER;
	gate.stackNumber = source->unitId();
	gate.gatingCreature = creatureByName("core:imp");
	gate.aimToHex(destination);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), gate));
	endRound();

	const auto & gatedState = battle()->getSide(BattleSide::ATTACKER).gatedDemonicStacks;
	ASSERT_EQ(gatedState.size(), 1u);
	auto * gated = const_cast<CStack *>(battle()->battleGetStackByID(gatedState.front().unitId, false));
	ASSERT_NE(gated, nullptr);
	const auto enemyHex = adjacentEnemyHex(gated);
	ASSERT_TRUE(enemyHex.isAvailable());
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:imp"), enemyHex, 1);
	ASSERT_NE(enemy, nullptr);

	// Give the gated stack one deterministic creature spell with enough power to kill the test
	// target. The cast still follows the normal BattleSpellMechanics -> Lua damage script path.
	gated->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELLCASTER, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW))));
	gated->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID()));
	gated->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::CREATURE_SPELL_POWER, BonusSource::OTHER, 1000, BonusSourceID()));

	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	spells::BattleCast cast(battle(), gated, spells::Mode::CREATURE_ACTIVE, spell);
	spells::Target target{spells::Destination(enemy)};
	const auto mechanics = spell->battleMechanics(&cast);
	ASSERT_TRUE(mechanics->canBeCastAt(target));
	server.injuries.clear();
	cast.cast(gameHandler->spellEnv.get(), target);

	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).chainGateArmed);
	EXPECT_TRUE(std::ranges::any_of(server.injuries, [enemy, gatedState](const auto & injury)
	{
		return std::ranges::any_of(injury.stacks, [enemy, gatedState](const auto & hit)
		{
			return hit.stackAttacked == enemy->unitId() && hit.attackerID == gatedState.front().unitId
				&& hit.killed() && !hit.cloneKilled() && !hit.willRebirth();
		});
	}));
}

TEST_F(NewHorizonsDemonicGatingTest, HypnotizedGatedUnitCannotRewardOriginalSideForFriendlyKill)
{
	grantChainGate();
	const auto * source = battle()->battleActiveUnit();
	ASSERT_NE(source, nullptr);
	const auto destination = legalGateHex(source);
	ASSERT_TRUE(destination.isAvailable());

	BattleAction gate;
	gate.actionType = EActionType::DEMONIC_GATING;
	gate.side = BattleSide::ATTACKER;
	gate.stackNumber = source->unitId();
	gate.gatingCreature = creatureByName("core:imp");
	gate.aimToHex(destination);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), gate));
	endRound();

	const auto & gatedState = battle()->getSide(BattleSide::ATTACKER).gatedDemonicStacks;
	ASSERT_EQ(gatedState.size(), 1u);
	auto * gated = battle()->getStack(static_cast<int>(gatedState.front().unitId), false);
	ASSERT_NE(gated, nullptr);

	BattleHex friendlyHex;
	const auto accessibility = battle()->getAccessibility();
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex candidate(index);
		if(candidate.isAvailable() && BattleHex::getDistance(gated->getPosition(), candidate) == 1
			&& accessibility.accessible(candidate, false, BattleSide::ATTACKER)
			&& !battle()->battleGetUnitByPos(candidate, true))
		{
			friendlyHex = candidate;
			break;
		}
	}
	ASSERT_TRUE(friendlyHex.isAvailable());
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), friendlyHex, 1);
	ASSERT_NE(friendly, nullptr);
	blockRetaliation(friendly);
	forceMaximumDamage(gated);
	gated->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(gated->isHypnotized());

	battle()->activeStack = gated->unitId();
	server.attacks.clear();
	const auto action = BattleAction::makeMeleeAttack(gated, friendly->getPosition(), gated->getPosition());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), action));
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).chainGateArmed);
	EXPECT_FALSE(std::ranges::any_of(server.attacks, [](const auto & attack)
	{
		return attack.chainGateTriggered;
	}));
}

TEST_F(NewHorizonsDemonicGatingTest, RejectedChainGateMarkerLeavesBattleStateUntouched)
{
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	BattleAttack attack;
	attack.battleID = BattleID(0);
	attack.stackAttacking = active->unitId();
	attack.chainGateTriggered = true;
	attack.fortuneSide = BattleSide::ATTACKER;
	attack.fortuneState = SylvanLuckState{};
	attack.fortuneState->serendipity = true;

	EXPECT_THROW(gameState()->apply(attack), std::runtime_error);
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).chainGateArmed);
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).sylvanLuck.serendipity);
}

TEST_F(NewHorizonsDemonicGatingTest, CloneOrRebirthMarkedKillsDoNotArmChainGate)
{
	grantChainGate();
	const auto * source = battle()->battleActiveUnit();
	ASSERT_NE(source, nullptr);
	const auto destination = legalGateHex(source);
	ASSERT_TRUE(destination.isAvailable());

	BattleAction gate;
	gate.actionType = EActionType::DEMONIC_GATING;
	gate.side = BattleSide::ATTACKER;
	gate.stackNumber = source->unitId();
	gate.gatingCreature = creatureByName("core:imp");
	gate.aimToHex(destination);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), gate));
	endRound();

	const auto & gated = battle()->getSide(BattleSide::ATTACKER).gatedDemonicStacks;
	ASSERT_EQ(gated.size(), 1u);
	const auto * gatedStack = battle()->battleGetStackByID(gated.front().unitId, false);
	ASSERT_NE(gatedStack, nullptr);
	const auto enemyHex = adjacentEnemyHex(gatedStack);
	ASSERT_TRUE(enemyHex.isAvailable());
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), enemyHex, 1);
	ASSERT_NE(enemy, nullptr);

	StacksInjured injury;
	injury.battleID = BattleID(0);
	for(const auto flags : {BattleStackAttacked::KILLED | BattleStackAttacked::CLONE_KILLED,
		BattleStackAttacked::KILLED | BattleStackAttacked::REBIRTH})
	{
		BattleStackAttacked hit;
		hit.attackerID = gated.front().unitId;
		hit.stackAttacked = enemy->unitId();
		hit.flags = flags;
		hit.newState.id = enemy->unitId();
		hit.newState.operation = UnitChanges::EOperation::UPDATE;
		hit.newState.data = enemy->acquireState()->save();
		injury.stacks.push_back(std::move(hit));
	}
	EXPECT_NO_THROW(gameState()->apply(injury));
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).chainGateArmed);
}

TEST(NewHorizonsDemonicGatingRules, EndlessLegionRestoresHalfOfGatedCasualtiesRoundedDown)
{
	SideInBattle::GatedDemonicStack gated;
	gated.initialCount = 12;
	EXPECT_EQ(gated.endlessLegionRestoration(12), 0);
	EXPECT_EQ(gated.endlessLegionRestoration(5), 3);
	EXPECT_EQ(gated.endlessLegionRestoration(0), 6);
	EXPECT_EQ(gated.endlessLegionRestoration(20), 0);
}

TEST(NewHorizonsDemonicGatingWire, ChainGateStateAndAttackMarkerRoundTripOnlyOnNewProtocol)
{
	SideInBattle outgoing(nullptr);
	outgoing.chainGateArmed = true;
	SideInBattle::PendingDemonicGate accelerated;
	accelerated.chainGateAccelerated = true;
	outgoing.pendingDemonicGates.push_back(accelerated);
	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & outgoing);
	SideInBattle restored(nullptr);
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_TRUE(restored.chainGateArmed);
	ASSERT_EQ(restored.pendingDemonicGates.size(), 1u);
	EXPECT_TRUE(restored.pendingDemonicGates.front().chainGateAccelerated);

	CMemorySerializer oldSide;
	oldSide.oser.version = ESerializationVersion::NEW_HORIZONS_DEMONIC_RESERVE;
	EXPECT_THROW(oldSide.oser & outgoing, std::runtime_error);
	EXPECT_TRUE(oldSide.extractBuffer().empty());

	SideInBattle acceleratedOnly(nullptr);
	acceleratedOnly.pendingDemonicGates.push_back(accelerated);
	CMemorySerializer oldAccelerated;
	oldAccelerated.oser.version = ESerializationVersion::NEW_HORIZONS_DEMONIC_RESERVE;
	EXPECT_THROW(oldAccelerated.oser & acceleratedOnly, std::runtime_error);
	EXPECT_TRUE(oldAccelerated.extractBuffer().empty());

	BattleAttack attack;
	attack.battleID = BattleID(7);
	attack.stackAttacking = 17;
	attack.chainGateTriggered = true;
	CMemorySerializer attackWire;
	attackWire.oser.version = ESerializationVersion::CURRENT;
	attackWire.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(attackWire.oser & attack);
	BattleAttack restoredAttack;
	ASSERT_NO_THROW(attackWire.iser & restoredAttack);
	EXPECT_TRUE(restoredAttack.chainGateTriggered);

	CMemorySerializer oldAttack;
	oldAttack.oser.version = ESerializationVersion::NEW_HORIZONS_DEMONIC_RESERVE;
	EXPECT_THROW(oldAttack.oser & attack, std::runtime_error);
	EXPECT_TRUE(oldAttack.extractBuffer().empty());

	BattleDemonicGatingStateChanged snapshot;
	snapshot.battleID = BattleID(7);
	snapshot.side = BattleSide::ATTACKER;
	snapshot.chainGateArmed = true;
	snapshot.pending.push_back(accelerated);
	CMemorySerializer snapshotWire;
	snapshotWire.oser.version = ESerializationVersion::CURRENT;
	snapshotWire.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(snapshotWire.oser & snapshot);
	BattleDemonicGatingStateChanged restoredSnapshot;
	ASSERT_NO_THROW(snapshotWire.iser & restoredSnapshot);
	EXPECT_TRUE(restoredSnapshot.chainGateArmed);
	ASSERT_EQ(restoredSnapshot.pending.size(), 1u);
	EXPECT_TRUE(restoredSnapshot.pending.front().chainGateAccelerated);

	BattleDemonicGatingStateChanged oldSnapshot = snapshot;
	CMemorySerializer oldSnapshotWire;
	oldSnapshotWire.oser.version = ESerializationVersion::NEW_HORIZONS_DEMONIC_RESERVE;
	EXPECT_THROW(oldSnapshotWire.oser & oldSnapshot, std::runtime_error);
	EXPECT_TRUE(oldSnapshotWire.extractBuffer().empty());
}
