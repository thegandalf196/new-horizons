/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsSpecialtyProcFixture.h"
#include "../../../lib/battle/NewHorizonsCreatureBurn.h"
class NewHorizonsStunLifetimeTest : public NewHorizonsSpecialtyProcFixture {};
TEST_F(NewHorizonsStunLifetimeTest, LethalSecondWindFireWallConsumesTheStartedSlotAndClearsItsPendingState)
{
	prepare("core:yog", "core:cyclop");
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:command")), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:havocMagic")), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 1000, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::FIRE_WALL); setTestSpellPointTotal(attackerSideHero, 1000);
	noMorale(*actor); noMorale(*enemy);
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	BattleAction wall; wall.actionType = EActionType::HERO_SPELL; wall.side = BattleSide::ATTACKER;
	wall.spell = SpellID::FIRE_WALL; wall.aimToHex(BattleHex(70)); wall.spellFireWallDirection = BattleHex::RIGHT;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), wall));
	ASSERT_EQ(battle()->obstacles.size(), 1);
	advanceRound();
	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(70), 1);
	target->movedThisRound = true;
	status(*target, newHorizonsStun::marker());
	const auto targetId = target->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::SECOND_WIND, targetId)));
	const auto * current = gameState()->getBattle(BattleID(0));
	ASSERT_NE(current, nullptr);
	const auto * body = current->battleGetUnitByID(targetId);
	ASSERT_NE(body, nullptr); EXPECT_FALSE(body->alive());
	EXPECT_FALSE(newHorizonsStun::isStunned(*body));
	EXPECT_EQ(forfeitures(targetId), 0); // A corpse does not receive an invented action.
	const auto order = current->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND);
	ASSERT_TRUE(order.has_value()); EXPECT_FALSE(order->secondWindActive);
	ASSERT_NE(current->battleActiveUnit(), nullptr); EXPECT_NE(current->battleActiveUnit()->unitId(), targetId);
}
TEST_F(NewHorizonsStunLifetimeTest, LethalStartEffectCanFinishTheBattleWithoutReadingRetiredOwners)
{
	prepare("core:yog", "core:cyclop");
	noMorale(*actor); noMorale(*enemy);
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	status(*enemy, newHorizonsStun::marker());
	status(*enemy, newHorizonsCreatureBurn::marker(HeroTypeID(HeroTypeID::decode("core:rashka")), actor->unitId(), 1000000));
	const auto targetId = enemy->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeDefend(actor)));
	const auto * current = gameState()->getBattle(BattleID(0));
	EXPECT_TRUE(!current || current->battleIsFinished().has_value());
	EXPECT_EQ(forfeitures(targetId), 0);
}
