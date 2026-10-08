/*
 * HeroOrderTargetRejectionTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt file in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "FocusFireFixture.h"

#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/texts/CGeneralTextHandler.h"

class HeroOrderTargetRejectionTest : public HeroCommandFixture
{
protected:
	using Reason = heroCommands::TargetRejection;
	CStack * protector = nullptr;
	CStack * ward = nullptr;
	CStack * enemy = nullptr;

	void prepareTargets()
	{
		startGame();
		startBattle();
		BattleUnitsChanged changes;
		changes.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			changes.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(changes);
		protector = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(70), 10);
		ward = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(72), 10);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 10);
		beginCombat();
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = protector->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
	}

	Reason reason(HeroCommand command, std::vector<uint32_t> ids, bool selectingProtector = false)
	{
		return battle()->battleGetHeroOrderTargetRejection(BattleSide::ATTACKER, command, ids, selectingProtector);
	}

	void expectReason(HeroCommand command, std::vector<uint32_t> ids, Reason expected)
	{
		EXPECT_EQ(reason(command, ids), expected);
		EXPECT_EQ(battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER, command, ids).has_value(),
			expected == Reason::NONE);
	}

	void moveTo(CStack * stack, BattleHex position)
	{
		BattleStackMoved moved;
		moved.battleID = BattleID(0);
		moved.stack = stack->unitId();
		moved.tilesToMove.insert(position);
		gameHandler->sendAndApply(moved);
	}

	std::vector<std::byte> randomState()
	{
		CMemorySerializer writer;
		gameHandler->randomizer->serialize(writer.oser);
		return writer.extractBuffer();
	}
};

TEST_F(HeroOrderTargetRejectionTest, ProtectStagesAndFullFootprintAdjacencyMatchPreparation)
{
	prepareTargets();
	ASSERT_TRUE(protector->doubleWide());
	ASSERT_TRUE(ward->doubleWide());
	ASSERT_GT(BattleHex::getDistance(protector->getPosition(), ward->getPosition()), 1);
	EXPECT_EQ(reason(HeroCommand::PROTECT, {protector->unitId()}, true), Reason::NONE);
	expectReason(HeroCommand::PROTECT, {protector->unitId()}, Reason::TARGET_COUNT);
	expectReason(HeroCommand::PROTECT, {protector->unitId(), ward->unitId()}, Reason::NONE);
	expectReason(HeroCommand::PROTECT, {protector->unitId(), protector->unitId()}, Reason::SAME_STACK);
	expectReason(HeroCommand::PROTECT, {protector->unitId(), enemy->unitId()}, Reason::FRIENDLY_REQUIRED);
	moveTo(ward, BattleHex(120));
	expectReason(HeroCommand::PROTECT, {protector->unitId(), ward->unitId()}, Reason::NOT_ADJACENT);
	EXPECT_EQ(reason(HeroCommand::PROTECT, {protector->unitId()}, true), Reason::NO_ADJACENT_WARD);
}

TEST_F(HeroOrderTargetRejectionTest, OrdinaryLivingOwnershipAndStaleProtectorAreSharedChecks)
{
	prepareTargets();
	EXPECT_EQ(reason(HeroCommand::PROTECT, {enemy->unitId()}, true), Reason::FRIENDLY_REQUIRED);
	EXPECT_EQ(reason(HeroCommand::PROTECT, {std::numeric_limits<uint32_t>::max()}, true), Reason::NO_STACK);
	ward->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SIEGE_WEAPON, BonusSource::OTHER, 1, BonusSourceID()));
	expectReason(HeroCommand::PROTECT, {protector->unitId(), ward->unitId()}, Reason::ORDINARY_REQUIRED);
	EXPECT_EQ(reason(HeroCommand::PROTECT, {ward->unitId()}, true), Reason::ORDINARY_REQUIRED);

	auto dead = protector->acquireState();
	int64_t damage = dead->getAvailableHealth();
	dead->damage(damage);
	BattleUnitsChanged killed;
	killed.battleID = BattleID(0);
	killed.changedStacks.emplace_back(protector->unitId(), UnitChanges::EOperation::UPDATE);
	killed.changedStacks.back().data = dead->save();
	killed.changedStacks.back().healthDelta = -damage;
	gameHandler->sendAndApply(killed);
	expectReason(HeroCommand::PROTECT, {protector->unitId(), enemy->unitId()}, Reason::NOT_LIVING);
}

TEST_F(HeroOrderTargetRejectionTest, SecondWindUsesExistingMovedDefendAndWaitRules)
{
	prepareTargets();
	expectReason(HeroCommand::SECOND_WIND, {enemy->unitId()}, Reason::FRIENDLY_REQUIRED);
	expectReason(HeroCommand::SECOND_WIND, {ward->unitId()}, Reason::ACTIVATION_UNSPENT);
	ward->waiting = true;
	ward->waitedThisTurn = true;
	expectReason(HeroCommand::SECOND_WIND, {ward->unitId()}, Reason::ACTIVATION_UNSPENT);
	ward->waiting = false;
	ward->movedThisRound = true;
	expectReason(HeroCommand::SECOND_WIND, {ward->unitId()}, Reason::NONE);
	ward->movedThisRound = false;
	ward->defending = true;
	expectReason(HeroCommand::SECOND_WIND, {ward->unitId()}, Reason::NONE);
	ward->defending = false;
	expectReason(HeroCommand::SECOND_WIND, {ward->unitId()}, Reason::ACTIVATION_UNSPENT);
}

TEST_F(HeroOrderTargetRejectionTest, EnemyOrdersAndUnavailableContextKeepOriginalLegality)
{
	prepareTargets();
	expectReason(HeroCommand::FLANK, {ward->unitId()}, Reason::ENEMY_REQUIRED);
	expectReason(HeroCommand::FLANK, {enemy->unitId()}, Reason::NONE);
	expectReason(HeroCommand::FOCUS_FIRE, {ward->unitId()}, Reason::ENEMY_REQUIRED);
	expectReason(HeroCommand::FOCUS_FIRE, {enemy->unitId()}, Reason::NO_FOCUS_RECIPIENT);
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(120), 10);
	expectReason(HeroCommand::FOCUS_FIRE, {enemy->unitId()}, Reason::NONE);
	shooter->movedThisRound = true;
	expectReason(HeroCommand::FOCUS_FIRE, {enemy->unitId()}, Reason::NONE);
	shooter->shots.use(shooter->shots.available());
	expectReason(HeroCommand::FOCUS_FIRE, {enemy->unitId()}, Reason::NO_FOCUS_RECIPIENT);
	EXPECT_FALSE(battle()->battleCanConfirmHeroCommand(BattleSide::ATTACKER, HeroCommand::FLANK, enemy->unitId()));
	for(const auto side : {BattleSide::NONE, static_cast<BattleSide>(127), BattleSide::DEFENDER})
		EXPECT_EQ(battle()->battleGetHeroOrderTargetRejection(side, HeroCommand::PROTECT,
			{protector->unitId(), ward->unitId()}), Reason::UNAVAILABLE);
	expectReason(HeroCommand::NONE, {}, Reason::UNAVAILABLE);
	const auto heroID = battle()->getSide(BattleSide::ATTACKER).heroID;
	battle()->getSide(BattleSide::ATTACKER).heroID = ObjectInstanceID::NONE;
	expectReason(HeroCommand::FLANK, {enemy->unitId()}, Reason::UNAVAILABLE);
	battle()->getSide(BattleSide::ATTACKER).heroID = heroID;
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	expectReason(HeroCommand::FLANK, {enemy->unitId()}, Reason::UNAVAILABLE);
}

TEST_F(HeroOrderTargetRejectionTest, UnsupportedRulesCannotOfferAnOrderReasonOfNone)
{
	useCommands = false;
	prepareTargets();
	expectReason(HeroCommand::PROTECT, {protector->unitId(), ward->unitId()}, Reason::UNAVAILABLE);
	EXPECT_EQ(reason(HeroCommand::PROTECT, {protector->unitId()}, true), Reason::UNAVAILABLE);
	expectReason(HeroCommand::FOCUS_FIRE, {enemy->unitId()}, Reason::UNAVAILABLE);
}

TEST_F(HeroOrderTargetRejectionTest, FocusFireDistinguishesLivingUnavailableFromDeadTargets)
{
	prepareTargets();
	addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(120), 10);
	expectReason(HeroCommand::FOCUS_FIRE, {enemy->unitId()}, Reason::NONE);
	const Bonus stopped(BonusDuration::ONE_BATTLE,
		BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	enemy->addNewBonus(std::make_shared<Bonus>(stopped));
	ASSERT_TRUE(enemy->alive());
	ASSERT_FALSE(enemy->isValidTarget());
	expectReason(HeroCommand::FOCUS_FIRE, {enemy->unitId()}, Reason::TARGET_UNAVAILABLE);
	EXPECT_FALSE(battle()->battleCanConfirmHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE, enemy->unitId()));
	// Time Stop correctly ignores damage. Remove the fixture marker through the
	// normal effect-removal packet before constructing the separate dead target.
	SetStackEffect unstopped;
	unstopped.battleID = BattleID(0);
	unstopped.toRemove.emplace_back(enemy->unitId(), std::vector<Bonus>{stopped});
	gameHandler->sendAndApply(unstopped);
	ASSERT_FALSE(enemy->isTimeStopped());
	ASSERT_TRUE(enemy->isValidTarget());
	auto dead = enemy->acquireState();
	int64_t damage = dead->getAvailableHealth();
	dead->damage(damage);
	ASSERT_FALSE(dead->alive());
	BattleUnitsChanged killed;
	killed.battleID = BattleID(0);
	killed.changedStacks.emplace_back(enemy->unitId(), UnitChanges::EOperation::UPDATE);
	killed.changedStacks.back().data = dead->save();
	killed.changedStacks.back().healthDelta = -damage;
	gameHandler->sendAndApply(killed);
	ASSERT_FALSE(enemy->alive());
	expectReason(HeroCommand::FOCUS_FIRE, {enemy->unitId()}, Reason::NOT_LIVING);
}

TEST_F(HeroOrderTargetRejectionTest, RegisteredRejectionTextsMatchStatusBarKeys)
{
	startGame();
	const std::pair<const char *, const char *> texts[] = {
		{"unavailable", "This Order is no longer available."},
		{"targetCount", "Select the required number of stacks."},
		{"noStack", "Select a stack."},
		{"notLiving", "Select a living stack."},
		{"targetUnavailable", "This stack cannot currently be targeted."},
		{"friendlyRequired", "Select a stack you control."},
		{"enemyRequired", "Select an enemy stack."},
		{"ordinaryRequired", "Select an ordinary creature stack."},
		{"invulnerable", "This target is invulnerable."},
		{"noFocusRecipient", "No eligible friendly stack can attack this target."},
		{"noMeleeRecipient", "No eligible friendly melee stack can receive this Order."},
		{"sameStack", "Protector and Ward must be different stacks."},
		{"notAdjacent", "Protector and Ward must be adjacent."},
		{"noAdjacentWard", "This Protector has no legal adjacent Ward."},
		{"activationUnspent", "This stack has not spent its activation."},
		{"noRecipient", "No eligible friendly stack can receive this Order."},
		{"generic", "This stack is not a legal target."}
	};
	for(const auto & [suffix, expected] : texts)
	{
		const std::string key = std::string("new-horizons.combat.orders.targetRejection.") + suffix;
		const auto translated = LIBRARY->generaltexth->translate(key);
		EXPECT_NE(translated, key);
		EXPECT_EQ(translated, expected) << key;
	}
}

TEST_F(HeroOrderTargetRejectionTest, RepeatedQueriesDoNotChangeUnitsAllowancesManaRngOrPackets)
{
	prepareTargets();
	const auto allowances = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	const auto first = protector->acquireState()->save();
	const auto second = ward->acquireState()->save();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto rng = randomState();
	const auto actions = server.startedActions.size();
	const auto orders = server.orderStateUpdates.size();
	for(int i = 0; i < 5; ++i)
	{
		EXPECT_EQ(reason(HeroCommand::PROTECT, {protector->unitId()}, true), Reason::NONE);
		expectReason(HeroCommand::PROTECT, {protector->unitId(), ward->unitId()}, Reason::NONE);
		expectReason(HeroCommand::SECOND_WIND, {ward->unitId()}, Reason::ACTIVATION_UNSPENT);
		expectReason(HeroCommand::FLANK, {enemy->unitId()}, Reason::NONE);
	}
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), allowances);
	EXPECT_EQ(protector->acquireState()->save(), first);
	EXPECT_EQ(ward->acquireState()->save(), second);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(randomState(), rng);
	EXPECT_EQ(server.startedActions.size(), actions);
	EXPECT_EQ(server.orderStateUpdates.size(), orders);
}

class LegacyHeroOrderTargetRejectionTest : public FocusFireFixture
{
};

TEST_F(LegacyHeroOrderTargetRejectionTest, FocusFireReasonsPreserveStaticShotAndLegacyPreparation)
{
	prepareFocus();
	using Reason = heroCommands::TargetRejection;
	const auto query = [&]()
	{
		return battle()->battleGetHeroOrderTargetRejection(BattleSide::ATTACKER,
			HeroCommand::FOCUS_FIRE, {target->unitId()});
	};
	EXPECT_EQ(query(), Reason::NONE);
	EXPECT_TRUE(battle()->battlePrepareFocusFireState(BattleSide::ATTACKER, target->unitId()));
	EXPECT_FALSE(battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE, {target->unitId()}));
	shooter->movedThisRound = true;
	EXPECT_EQ(query(), Reason::NONE);
	shooter->shots.use(shooter->shots.available());
	EXPECT_EQ(query(), Reason::NO_FOCUS_RECIPIENT);
	EXPECT_FALSE(battle()->battlePrepareFocusFireState(BattleSide::ATTACKER, target->unitId()));
}
