/*
 * HeroQueueCompletionSelectorTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../../lib/battle/NewHorizonsBerserk.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/battle/NewHorizonsPlague.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"

#include <algorithm>

namespace heroQueueTest
{
TEST_F(HeroQueueSelectorTest, FrozenNormalQueueSlotPrecedesEnemyAndCureRemainsLegal)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(injure(actor, actor->getAvailableHealth() - 1000));
	ASSERT_NO_FATAL_FAILURE(freeze(actor));
	ASSERT_NO_FATAL_FAILURE(expectBeforeEnemy(actor));
	ASSERT_TRUE(battle()->getHeroActionAllowances(BattleSide::DEFENDER).eligibleAllowance(
		HeroActionAllowanceState::ActionKind::SPELL, battle()->getRound()));
	ASSERT_TRUE(attackerSideHero->getSpellsInSpellbook().contains(SpellID::CURE));
	const auto * spell = SpellID(SpellID::CURE).toSpell();
	spells::BattleCast cure(battle(), attackerSideHero, spells::Mode::HERO, spell);
	cure.setCurePhysicalAffliction("frozen");
	spells::Target target;
	target.emplace_back(actor);
	ASSERT_TRUE(spell->battleMechanics(&cure)->canBeCastAt(target));
	ASSERT_NO_FATAL_FAILURE(characterize(true));
	if(!callback->heroRequests.empty() && callback->heroRequests.front().spell == SpellID::CURE)
	{
		EXPECT_EQ(callback->heroRequests.front().spellCurePhysicalAffliction, "frozen");
		EXPECT_EQ(callback->heroRequests.front().spellCureAffliction, SpellID::NONE);
	}
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*actor));
}

void HeroQueueSelectorTest::infectedForfeit(bool lethal)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(selectPerk(defenderSideHero, newHorizonsOffense::SKILL,
		"new-horizons:offense.shockAssault", MasteryLevel::BASIC));
	ASSERT_NO_FATAL_FAILURE(selectPerk(defenderSideHero, newHorizonsOffense::SKILL,
		newHorizonsOffense::VENGEANCE, MasteryLevel::ADVANCED));
	ASSERT_NO_FATAL_FAILURE(selectPerk(defenderSideHero, newHorizonsOffense::SKILL,
		newHorizonsOffense::NO_QUARTER, MasteryLevel::EXPERT));
	const SpellID plague(SpellID::decode(std::string(newHorizonsPlague::SPELL_ID)));
	ASSERT_TRUE(plague.hasValue());
	ASSERT_NO_FATAL_FAILURE(book(defenderSideHero, plague, "new-horizons:shadowMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(activate(enemy));
	ASSERT_TRUE(cast(defenderSideHero, plague, actor));
	ASSERT_TRUE(newHorizonsPlague::hasPlague(actor));
	ASSERT_FALSE(battle()->getHeroActionAllowances(BattleSide::DEFENDER).eligibleAllowance(
		HeroActionAllowanceState::ActionKind::SPELL, battle()->getRound()));
	const auto worstHit = battle()->calculateDmgRange(BattleAttackInfo(enemy, actor, 0, false)).damage.max;
	const auto health = actor->getTotalHealth() / 5;
	ASSERT_GT(health, worstHit + 200);
	ASSERT_NO_FATAL_FAILURE(injure(actor, health));
	ASSERT_NO_FATAL_FAILURE(activate(enemy));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeMeleeAttack(enemy, actor->getPosition(), enemy->getPosition())));
	ASSERT_TRUE(actor->alive());
	ASSERT_EQ(actor->noQuarterMoraleActivationsRemaining, 1);
	ASSERT_TRUE(actor->hasBonus(CSelector(newHorizonsOffense::isNoQuarterMoralePenalty)));
	if(lethal)
	{
		ASSERT_NO_FATAL_FAILURE(injure(actor, 1));
	}
	ASSERT_NO_FATAL_FAILURE(freeze(actor));
	// The real No Quarter attack already consumed enemy's normal action. Keep
	// that receipt intact and use a fresh defender as the remaining queue stop.
	enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(14, 5), 1000);
	ASSERT_NE(enemy, nullptr);
	ASSERT_EQ(enemy->unitSide(), BattleSide::DEFENDER);
	effect(enemy, Bonus(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
		BonusSource::OTHER, 400, BonusSourceID(), BonusSubtypeID(), BonusValueType::BASE_NUMBER));
	effect(enemy, Bonus(BonusDuration::ONE_BATTLE, BonusType::NO_MORALE, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(enemy->willMove());
	ASSERT_FALSE(battle()->getHeroActionAllowances(BattleSide::DEFENDER).eligibleAllowance(
		HeroActionAllowanceState::ActionKind::SPELL, battle()->getRound()));
	ASSERT_NO_FATAL_FAILURE(activate(active));
	ASSERT_NO_FATAL_FAILURE(expectBeforeEnemy(actor));
	ASSERT_FALSE(actor->moved());
	const auto markers = actor->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(plague))
		.And(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER)));
	ASSERT_EQ(markers->size(), 1u);
	const auto tick = newHorizonsPlague::adjustedTickDamage(*battle(), BattleSide::DEFENDER, actor, markers->front()->val);
	ASSERT_GT(tick, 0);
	if(lethal)
	{
		ASSERT_GT(tick, actor->getAvailableHealth());
	}
	else
	{
		ASSERT_LT(tick, actor->getAvailableHealth());
	}
	RecordProperty("forecastCompletionTick", std::to_string(tick));
	RecordProperty("lethalTickSetup", lethal ? "1" : "0");
}

TEST_F(HeroQueueSelectorTest, FrozenPlagueAndNoQuarterNonlethalCompletionStayBranchLocal)
{
	ASSERT_NO_FATAL_FAILURE(infectedForfeit(false));
	ASSERT_NO_FATAL_FAILURE(characterize(true));
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*actor));
	EXPECT_TRUE(newHorizonsPlague::hasPlague(actor));
	EXPECT_EQ(actor->noQuarterMoraleActivationsRemaining, 1);
}

TEST_F(HeroQueueSelectorTest, FrozenPlagueLethalCompletionCannotThawLiveOrRetainedBranches)
{
	ASSERT_NO_FATAL_FAILURE(infectedForfeit(true));
	ASSERT_EQ(actor->getAvailableHealth(), 1);
	ASSERT_NO_FATAL_FAILURE(characterize(true));
	EXPECT_TRUE(actor->alive());
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*actor));
	EXPECT_EQ(actor->getAvailableHealth(), 1);
}

TEST_F(HeroQueueSelectorTest, PreexistingBerserkAttackFinishesNoCastForecastWithoutLiveMutation)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(book(defenderSideHero, SpellID::BERSERK, "new-horizons:chaosMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(activate(enemy));
	ASSERT_TRUE(cast(defenderSideHero, SpellID::BERSERK, actor));
	ASSERT_NO_FATAL_FAILURE(activate(active));
	ASSERT_TRUE(actor->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
	ASSERT_FALSE(battle()->battleGetDivineMandateStatus(BattleSide::ATTACKER).active);
	ASSERT_FALSE(battle()->battleCanUseMetamagicFollowup(BattleSide::ATTACKER));
	ASSERT_GT(battle()->calculateDmgRange(BattleAttackInfo(actor, enemy, 0, false)).damage.min,
		enemy->getAvailableHealth());
	ASSERT_TRUE(std::ranges::any_of(battle()->getBerserkForcedActions(actor), [](const auto & action)
	{
		return action.type == EActionType::WALK_AND_ATTACK || action.type == EActionType::SHOOT;
	}));
	for(const auto & action : battle()->getBerserkForcedActions(actor))
		ASSERT_EQ(action.target, enemy);
	ASSERT_NO_FATAL_FAILURE(expectBeforeEnemy(actor));
	ASSERT_FALSE(newHorizonsBerserk::completedForcedActivationBonuses(*battle(), actor).empty());
	ASSERT_NO_FATAL_FAILURE(characterize(false));
	EXPECT_TRUE(callback->heroRequests.empty());
	EXPECT_TRUE(actor->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
}

TEST_F(HeroQueueSelectorTest, PreexistingBerserkWithoutReachableTargetRetainsLiveMarker)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(book(defenderSideHero, SpellID::BERSERK, "new-horizons:chaosMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(activate(enemy));
	ASSERT_TRUE(cast(defenderSideHero, SpellID::BERSERK, actor));
	ASSERT_NO_FATAL_FAILURE(activate(active));
	effect(actor, Bonus(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
		BonusSource::OTHER, -static_cast<int64_t>(actor->getMovementRange()), BonusSourceID()));
	// All adjacent cells contain no reachable hostile unit only if the normal
	// target next to actor is removed; retain a remote enemy so battle is ongoing.
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	remove.changedStacks.emplace_back(enemy->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(14, 5), 100);
	ASSERT_NE(enemy, nullptr);
	ASSERT_EQ(actor->getMovementRange(), 0);
	ASSERT_FALSE(std::ranges::any_of(battle()->getBerserkForcedActions(actor), [](const auto & action)
	{
		return action.type == EActionType::WALK_AND_ATTACK || action.type == EActionType::SHOOT;
	}));
	ASSERT_NO_FATAL_FAILURE(expectBeforeEnemy(actor));
	ASSERT_NO_FATAL_FAILURE(characterize(true));
	EXPECT_TRUE(actor->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
}

TEST_F(HeroQueueSelectorTest, SyntheticTimeStopQueueSlotDoesNotMutateLivePlagueOrActivationState)
{
	ASSERT_NO_FATAL_FAILURE(world());
	// Supply a real offensive candidate while preserving the stopped/plagued
	// state and all canonical Orders.
	ASSERT_TRUE(attackerSideHero->hasSpellbook());
	attackerSideHero->removeAllSpells();
	attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
	const auto sorcery = SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic"));
	ASSERT_TRUE(sorcery.hasValue());
	attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	const SpellID plague(SpellID::decode(std::string(newHorizonsPlague::SPELL_ID)));
	ASSERT_NO_FATAL_FAILURE(book(defenderSideHero, plague, "new-horizons:shadowMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	// Keep the sole defender outside every friendly unit's initial melee reach.
	// The adjacent setup's actual no-cast forecast ended combat before its turn.
	BattleStackMoved reposition;
	reposition.battleID = BattleID(0);
	reposition.stack = enemy->unitId();
	reposition.teleporting = true;
	reposition.tilesToMove.insert(BattleHex(14, 5));
	gameHandler->sendAndApply(reposition);
	ASSERT_EQ(enemy->getPosition(), BattleHex(14, 5));
	ASSERT_NO_FATAL_FAILURE(activate(enemy));
	ASSERT_TRUE(cast(defenderSideHero, plague, actor));
	Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	stopped.parameters = std::make_shared<BonusParameters>(static_cast<int32_t>(BattleSide::ATTACKER));
	effect(actor, stopped);
	ASSERT_NO_FATAL_FAILURE(activate(active));
	ASSERT_TRUE(actor->isTimeStopped());
	ASSERT_FALSE(actor->timeStopTurnConsumed());
	ASSERT_EQ(actor->getMovementRange(), 0);
	for(const auto * unit : {active, actor, waiter})
	{
		ASSERT_FALSE(unit->isShooter());
		ASSERT_EQ(unit->valOfBonuses(BonusType::LONG_REACH), 0);
		ASSERT_LT(unit->getMovementRange() + 1,
			BattleHex::getDistance(unit->getPosition(), enemy->getPosition()));
	}
	ASSERT_NO_FATAL_FAILURE(expectBeforeEnemy(actor));
	const auto * strike = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_TRUE(strike->canBeCast(battle(), spells::Mode::HERO, attackerSideHero));
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, strike);
	spells::Target target;
	target.emplace_back(enemy);
	const auto mechanics = strike->battleMechanics(&cast);
	ASSERT_TRUE(mechanics->canBeCastAt(target));
	ASSERT_GT(mechanics->adjustEffectValue(enemy),
		battle()->calculateDmgRange(BattleAttackInfo(active, enemy, 0, false)).damage.max);
	ASSERT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	ASSERT_NO_FATAL_FAILURE(characterize(true));
	EXPECT_TRUE(actor->isTimeStopped());
	EXPECT_FALSE(actor->timeStopTurnConsumed());
	EXPECT_TRUE(newHorizonsPlague::hasPlague(actor));
}
}
