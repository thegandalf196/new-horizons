/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsSpecialtyProcFixture.h"
#include "../../../lib/battle/NewHorizonsCreatureBurn.h"
#include "../../../lib/battle/PhysicalAffliction.h"
#include "../../../lib/spells/CurePhysicalAffliction.h"
#include "../../../lib/spells/NewHorizonsPurify.h"
class NewHorizonsStunActivationTest : public NewHorizonsSpecialtyProcFixture
{
protected:
	void forceNextGoodMorale()
	{
		actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::MORALE,
			BonusSource::OTHER, 3, BonusSourceID()));
		const int morale = battle()->battleGetMorale(actor);
		ASSERT_GT(morale, 0);
		const auto army = battle()->getSideArmy(BattleSide::ATTACKER)->id;
		CMemorySerializer copy; gameHandler->randomizer->serialize(copy.oser);
		GameRandomizer expected(*gameState()); expected.serialize(copy.iser);
		expected.rollGoodMorale(army, morale); // Establish the independent native Morale stream.
		for(int attempt = 0; attempt < 256; ++attempt)
		{
			CMemorySerializer before; expected.serialize(before.oser);
			if(expected.rollGoodMorale(army, morale))
			{
				gameHandler->randomizer->serialize(before.iser);
				return;
			}
		}
		FAIL() << "No native positive Morale state within the bounded fixture";
	}
};
TEST_F(NewHorizonsStunActivationTest, NormalQueueAutomaticallyForfeitsOneActivationAndDoesNotOfferInput)
{
	prepare("core:yog", "core:cyclop");
	noMorale(*actor); noMorale(*enemy);
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	status(*enemy, newHorizonsStun::marker());
	const auto id = enemy->unitId();
	for(int actions = 0; actions < 16 && forfeitures(id) == 0; ++actions)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr); ASSERT_NE(active, enemy);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(active), BattleAction::makeDefend(active)));
	}
	EXPECT_EQ(forfeitures(id), 1);
	EXPECT_FALSE(newHorizonsStun::isStunned(*enemy));
	EXPECT_TRUE(enemy->alive());
}
TEST_F(NewHorizonsStunActivationTest, PendingStunPreservesOrdinaryRetaliation)
{
	prepare("core:yog", "core:cyclop");
	noMorale(*actor); noMorale(*enemy);
	beginCombat(); status(*enemy, newHorizonsStun::marker());
	ASSERT_TRUE(enemy->ableToRetaliate());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(actor, enemy, actor->getPosition())));
	EXPECT_EQ(std::ranges::count_if(server.attacks, [this](const auto & attack)
	{ return attack.counter() && attack.stackAttacking == enemy->unitId(); }), 1);
}
TEST_F(NewHorizonsStunActivationTest, TypedActualActivationPredicateIncludesExtrasAndPausesTimeStopAndContinuations)
{
	prepare("core:yog", "core:cyclop");
	status(*actor, newHorizonsStun::marker());
	for(const auto reason : {BattleUnitTurnReason::TURN_QUEUE, BattleUnitTurnReason::MORALE,
		BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION, BattleUnitTurnReason::AUTOMATIC_ACTION})
		EXPECT_TRUE(newHorizonsStun::forfeitsActivation(*battle(), *actor, reason));
	for(const auto reason : {BattleUnitTurnReason::HERO_SPELLCAST, BattleUnitTurnReason::UNIT_SPELLCAST,
		BattleUnitTurnReason::ACTION_REJECTED, BattleUnitTurnReason::PURSUIT_CONTINUATION,
		BattleUnitTurnReason::DREAD_CAVALRY_CONTINUATION, BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION,
		BattleUnitTurnReason::MASTER_GATE_CONTINUATION})
		EXPECT_FALSE(newHorizonsStun::forfeitsActivation(*battle(), *actor, reason));
	actor->afterWait();
	EXPECT_TRUE(newHorizonsStun::isStunned(*actor));
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_FALSE(newHorizonsStun::forfeitsActivation(*battle(), *actor, BattleUnitTurnReason::TURN_QUEUE));
	EXPECT_FALSE(newHorizonsStun::forfeitsActivation(*battle(), *actor, BattleUnitTurnReason::MORALE));
	EXPECT_TRUE(newHorizonsStun::isStunned(*actor));
}
TEST_F(NewHorizonsStunActivationTest, CurrentAttackCompletesAndEarnedMoraleConsumesStunInsteadOfLeavingItForNextRound)
{
	prepare("core:yog", "core:cyclop");
	noMorale(*enemy); blockRetaliation(actor);
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_NO_FATAL_FAILURE(forceNextGoodMorale());
	status(*actor, newHorizonsStun::marker()); // Models a status earned inside this in-progress activation.
	const auto before = enemy->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(actor, enemy, actor->getPosition())));
	EXPECT_LT(enemy->getAvailableHealth(), before);
	EXPECT_EQ(forfeitures(actor->unitId()), 1);
	EXPECT_FALSE(newHorizonsStun::isStunned(*actor));
	EXPECT_TRUE(std::ranges::any_of(server.stackActivations, [this](const auto & activation)
	{ return activation.stack == actor->unitId() && activation.reason == BattleUnitTurnReason::MORALE; }));
}
TEST_F(NewHorizonsStunActivationTest, ActualSecondWindIsForfeitedAndCannotLeaveItsActiveOrderFlag)
{
	prepare("core:yog", "core:cyclop");
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:command")), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	noMorale(*actor); noMorale(*enemy);
	auto * window = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(8, 5), 10);
	for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
		window->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
			BonusSource::OTHER, 900, BonusSourceID(), BonusSubtypeID(), bound));
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeDefend(actor)));
	ASSERT_EQ(battle()->battleActiveUnit(), window);
	status(*actor, newHorizonsStun::marker());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::SECOND_WIND, actor->unitId())));
	EXPECT_EQ(forfeitures(actor->unitId()), 1);
	EXPECT_FALSE(newHorizonsStun::isStunned(*actor));
	const auto order = battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND);
	ASSERT_TRUE(order.has_value()); EXPECT_FALSE(order->secondWindActive);
}
TEST_F(NewHorizonsStunActivationTest, NativeCureRemovesPendingPhysicalStunWithoutCreatingAnActivation)
{
	prepare("core:yog", "core:cyclop");
	noMorale(*actor); noMorale(*enemy);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::CURE); setTestSpellPointTotal(attackerSideHero, 100);
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	status(*actor, newHorizonsStun::marker());
	ASSERT_TRUE(spells::curePhysical::present(battle()->getMagicRules(), *actor, "stun"));
	const auto activation = battle()->getActivationSerial();
	BattleAction cure; cure.actionType = EActionType::HERO_SPELL; cure.side = BattleSide::ATTACKER;
	cure.spell = SpellID::CURE; cure.spellCurePhysicalAffliction = "stun"; cure.aimToUnit(actor);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), cure));
	EXPECT_FALSE(newHorizonsStun::isStunned(*actor));
	EXPECT_EQ(battle()->getActivationSerial(), activation);
	EXPECT_EQ(forfeitures(actor->unitId()), 0);
}
TEST_F(NewHorizonsStunActivationTest, PhysicalSourceGroupCleansingNeverRemovesUnrelatedSameOwnerBonuses)
{
	prepare("core:yog", "core:cyclop");
	const auto marker = newHorizonsStun::marker();
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL,
		BonusSource::HERO_SPECIAL, 2, marker.sid, BonusSubtypeID(PrimarySkill::DEFENSE)));
	status(*actor, marker);
	const auto affliction = physicalAfflictions::first(*actor);
	ASSERT_TRUE(affliction); EXPECT_EQ(affliction->kind, "stun");
	const auto removed = physicalAfflictions::removalPlan(*actor, *affliction);
	ASSERT_EQ(removed.size(), 1); EXPECT_TRUE(newHorizonsStun::isMarker(&removed.front()));
	EXPECT_TRUE(newHorizonsPurify::eligibleStacks(*battle(), BattleSide::ATTACKER, actor->getPosition(), 0, false).empty());
	const auto eligible = newHorizonsPurify::eligibleStacks(*battle(), BattleSide::ATTACKER, actor->getPosition(), 0, true);
	ASSERT_EQ(eligible.size(), 1); EXPECT_TRUE(eligible.front().stunAutomaticallyCleared);
}
