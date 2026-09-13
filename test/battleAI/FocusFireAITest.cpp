/*
 * FocusFireAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../server/battles/FocusFireFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/spells/CSpell.h"

namespace
{
class FocusEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit FocusEnvironment(std::shared_ptr<CGameState> state)
		: state(std::move(state))
	{
	}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class FocusCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;
	FocusCallback()
		: CBattleCallback(PlayerColor(0), nullptr)
	{
	}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};
}

class FocusFireAITest : public FocusFireFixture
{
protected:
	std::shared_ptr<FocusEnvironment> environment;
	std::shared_ptr<FocusCallback> callback;

	void prepareAI()
	{
		prepareFocus();
		environment = std::make_shared<FocusEnvironment>(gameState());
		callback = std::make_shared<FocusCallback>();
		callback->onBattleStarted(battle());
	}

	std::shared_ptr<HypotheticBattle> model()
	{
		return std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	}
};

TEST_F(FocusFireAITest, CurrentCacheRecomputesMarkControllerAndExpiryButKeepsOriginalComparison)
{
	ASSERT_NO_FATAL_FAILURE(prepareAI());
	auto state = model();
	DamageCache original;
	original.cacheDamage(shooter, target, state);
	DamageCache current(&original);
	EXPECT_EQ(current.getDamage(shooter, target, state), 150);
	const auto mark = battle()->battlePrepareFocusFireState(BattleSide::ATTACKER, target->unitId());
	ASSERT_TRUE(mark);
	state->setFocusFireState(BattleSide::ATTACKER, *mark);
	EXPECT_EQ(current.getDamage(shooter, target, state), 180);
	EXPECT_EQ(current.getOriginalDamage(shooter, target, state), 150);
	const Bonus control(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	state->addUnitBonus(target->unitId(), {control});
	const auto * controlled = state->battleGetUnitByID(target->unitId());
	ASSERT_EQ(state->battleGetOwner(controlled), PlayerColor(0));
	EXPECT_FALSE(state->battleIsFocusFireTargetActive(BattleSide::ATTACKER));
	EXPECT_EQ(state->battleTargetedRangedCommandPercent(shooter, controlled, true), 0);
	const auto changed = state->battleEstimateDamage(shooter, controlled, 0).damage;
	const auto changedMean = (changed.min + changed.max) / 2;
	ASSERT_NE(changedMean, 180);
	EXPECT_EQ(current.getDamage(shooter, controlled, state), changedMean);
	state->removeUnitBonus(target->unitId(), {control});
	EXPECT_TRUE(state->battleIsFocusFireTargetActive(BattleSide::ATTACKER));
	EXPECT_EQ(state->battleTargetedRangedCommandPercent(shooter,
		state->battleGetUnitByID(target->unitId()), true), 30);
	EXPECT_EQ(current.getDamage(shooter, state->battleGetUnitByID(target->unitId()), state), 180);
	EXPECT_FALSE(battle()->battleGetFocusFireState(BattleSide::ATTACKER));
	state->nextRound();
	EXPECT_FALSE(state->battleGetFocusFireState(BattleSide::ATTACKER));
	EXPECT_EQ(current.getDamage(shooter, target, state), 150);
	EXPECT_EQ(current.getOriginalDamage(shooter, target, state), 150);
	EXPECT_EQ(state->battleGetRound(), battle()->battleGetRound() + 1);
	const auto renewed = state->battlePrepareFocusFireState(BattleSide::ATTACKER, target->unitId());
	ASSERT_TRUE(renewed);
	EXPECT_EQ(renewed->issuedRound, state->battleGetRound());
	state->setFocusFireState(BattleSide::ATTACKER, *renewed);
	EXPECT_EQ(current.getDamage(shooter, target, state), 180);
	HypotheticBattle child(environment.get(), state);
	child.nextRound();
	EXPECT_FALSE(child.battleGetFocusFireState(BattleSide::ATTACKER));
	EXPECT_TRUE(state->battleGetFocusFireState(BattleSide::ATTACKER));
	EXPECT_FALSE(battle()->battleGetFocusFireState(BattleSide::ATTACKER));
}

TEST_F(FocusFireAITest, LiveAndCandidateRoundOrdersExpireIncludingNestedProjection)
{
	ASSERT_NO_FATAL_FAILURE(prepareAI());
	const auto metrics = [&](const std::shared_ptr<HypotheticBattle> & state)
	{
		const auto * own = state->battleGetUnitByID(shooter->unitId());
		const auto * enemy = state->battleGetUnitByID(target->unitId());
		return std::make_tuple(
			state->calculateDmgRange(BattleAttackInfo(own, enemy, 0, false)).damage.min,
			state->calculateDmgRange(BattleAttackInfo(enemy, own, 0, false)).damage.min,
			own->getMovementRange());
	};
	for(const auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE, HeroCommand::ADVANCE})
	{
		SCOPED_TRACE(static_cast<int>(command));
		const auto baseline = metrics(model());
		auto candidate = model();
		candidate->addUnitBonus(shooter->unitId(),
			heroCommands::bonuses(battle()->getHeroCommandRules(), command, *attackerSideHero));
		const auto boosted = metrics(candidate);
		ASSERT_NE(boosted, baseline);
		auto child = std::make_shared<HypotheticBattle>(environment.get(), candidate);
		EXPECT_EQ(metrics(child), boosted);
		child->nextRound();
		EXPECT_EQ(metrics(child), baseline);
		EXPECT_EQ(metrics(candidate), boosted);
		candidate->nextRound();
		EXPECT_EQ(metrics(candidate), baseline);
		EXPECT_EQ(metrics(model()), baseline);

		ASSERT_TRUE(submit(BattleAction::makeHeroCommand(BattleSide::ATTACKER, command)));
		auto liveOrder = model();
		const auto liveBoosted = metrics(liveOrder);
		ASSERT_NE(liveBoosted, baseline);
		liveOrder->nextRound();
		EXPECT_EQ(metrics(liveOrder), baseline);
		EXPECT_EQ(metrics(model()), liveBoosted); // Projection never ages authoritative bonuses.
		advanceRound();
		activate(shooter);
		EXPECT_EQ(metrics(model()), baseline);
	}
}

TEST_F(FocusFireAITest, DeadRecipientsAgeCommandsWithoutChangingDoctrineOrSpellDurationPolicy)
{
	ASSERT_NO_FATAL_FAILURE(prepareAI());
	auto state = model();
	const auto order = heroCommands::bonuses(battle()->getHeroCommandRules(), HeroCommand::ADVANCE,
		*attackerSideHero);
	state->addUnitBonus(shooter->unitId(), order);
	state->addUnitBonus(shooter->unitId(), heroCommands::bonuses(battle()->getHeroCommandRules(),
		HeroCommand::AGGRESSIVE, *attackerSideHero));
	Bonus spell(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, 7, BonusSourceID());
	spell.turnsRemain = 1;
	state->addUnitBonus(shooter->unitId(), {spell});
	auto unit = state->getForUpdate(shooter->unitId());
	const CSelector timedCommand([](const Bonus * bonus)
	{
		return bonus->source == BonusSource::HERO_COMMAND && Bonus::NTurns(bonus);
	});
	const CSelector doctrine([](const Bonus * bonus)
	{
		return bonus->source == BonusSource::HERO_COMMAND && bonus->duration == BonusDuration::ONE_BATTLE;
	});
	const auto doctrineCount = unit->getAllBonuses(doctrine)->size();
	ASSERT_GT(doctrineCount, 0u);
	const auto health = unit->getAvailableHealth();
	auto damage = health;
	unit->damage(damage);
	ASSERT_FALSE(unit->alive());
	state->nextRound();
	EXPECT_TRUE(unit->getAllBonuses(timedCommand)->empty());
	EXPECT_EQ(unit->getAllBonuses(doctrine)->size(), doctrineCount);
	const auto spells = unit->getAllBonuses(Selector::sourceTypeSel(BonusSource::SPELL_EFFECT));
	// Successor uses current authoritative-aligned spell aging as well as Order aging.
	EXPECT_TRUE(spells->empty());
	auto healing = health;
	unit->heal(healing, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	ASSERT_TRUE(unit->alive());
	EXPECT_TRUE(unit->getAllBonuses(timedCommand)->empty());
	state->addUnitBonus(shooter->unitId(), order);
	EXPECT_FALSE(unit->getAllBonuses(timedCommand)->empty()); // Newly added Order gets its own round.
	state->nextRound();
	EXPECT_TRUE(unit->getAllBonuses(timedCommand)->empty());
	EXPECT_EQ(unit->getAllBonuses(doctrine)->size(), doctrineCount);
	EXPECT_TRUE(shooter->alive());
	EXPECT_EQ(shooter->getAvailableHealth(), health);
}

TEST_F(FocusFireAITest, ActualExchangeDistinguishesRoundOrderFromPersistentControlOnlyAfterRoundBoundary)
{
	shooterCount = 100;
	ASSERT_NO_FATAL_FAILURE(prepareAI());
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SHOTS, BonusSource::OTHER, 10, BonusSourceID()));
	const auto score = [&](bool persistent, int rounds)
	{
		auto state = model();
		for(const auto * unit : state->battleGetUnitsIf([](const battle::Unit *) { return true; }))
			if(unit->unitId() != shooter->unitId() && unit->unitId() != target->unitId())
				state->removeUnit(unit->unitId()); // Isolated projected duel, no authoritative army mutation.
		auto effects = heroCommands::bonuses(battle()->getHeroCommandRules(), HeroCommand::HOLD_THE_LINE,
			*attackerSideHero);
		if(persistent)
			for(auto & bonus : effects)
			{
				// Test-only identical first-round control, not a new persistent Order rule.
				bonus.duration = BonusDuration::ONE_BATTLE;
				bonus.turnsRemain = 0;
			}
		state->addUnitBonus(shooter->unitId(), effects);
		const auto * own = state->battleGetUnitByID(shooter->unitId());
		const auto * enemy = state->battleGetUnitByID(target->unitId());
		DamageCache cache;
		cache.buildDamageCache(state, BattleSide::ATTACKER);
		PotentialTargets targets(own, cache, state);
		const auto attack = AttackPossibility::evaluate(BattleAttackInfo(own, enemy, 0, true),
			own->getPosition(), cache, state);
		BattleExchangeEvaluator evaluator(state, environment, 1.0f, rounds);
		evaluator.updateReachabilityMap(state);
		const auto queue = evaluator.getExchangeUnits(attack, 0, targets, state);
		EXPECT_GE(queue.units.size(), static_cast<size_t>(rounds));
		for(const auto & round : queue.units)
		{
			EXPECT_TRUE(std::any_of(round.begin(), round.end(), [&](const battle::Unit * unit)
			{
				return unit->unitId() == shooter->unitId();
			}));
			EXPECT_TRUE(std::any_of(round.begin(), round.end(), [&](const battle::Unit * unit)
			{
				return unit->unitId() == target->unitId();
			}));
		}
		return evaluator.evaluateExchange(attack, 0, targets, cache, state);
	};
	EXPECT_FLOAT_EQ(score(false, 1), score(true, 1));
	EXPECT_GT(score(true, 2), score(false, 2));
}

TEST_F(FocusFireAITest, HypotheticalAmmoCartUsesCurrentCartStateAndDoesNotInventAmmunition)
{
	ASSERT_NO_FATAL_FAILURE(prepareAI());
	shooter->shots.use(1);
	ASSERT_FALSE(shooter->canShoot());
	auto * cart = addStack(BattleSide::ATTACKER, creatureByName("core:ammoCart"),
		BattleHex(leftHex + 2 * GameConstants::BFIELD_WIDTH), 1);
	ASSERT_TRUE(shooter->canShoot());
	auto state = model();
	auto projected = state->getForUpdate(shooter->unitId());
	EXPECT_EQ(projected->shots.available(), 0);
	EXPECT_TRUE(projected->canShoot());
	state->removeUnit(cart->unitId());
	EXPECT_FALSE(projected->canShoot());
	EXPECT_EQ(projected->shots.available(), 0);
	EXPECT_TRUE(shooter->canShoot()); // Model removal must not mutate the real cart.
}

TEST_F(FocusFireAITest, ProjectedCartReplacementKeepsRealFirstMatchingOrder)
{
	ASSERT_NO_FATAL_FAILURE(prepareAI());
	shooter->shots.use(1);
	auto * first = addStack(BattleSide::ATTACKER, creatureByName("core:ammoCart"),
		BattleHex(leftHex + 2 * GameConstants::BFIELD_WIDTH), 1);
	auto * second = addStack(BattleSide::ATTACKER, creatureByName("core:ammoCart"),
		BattleHex(leftHex - 2 * GameConstants::BFIELD_WIDTH), 1);
	auto state = model();
	auto projected = state->getForUpdate(shooter->unitId());
	ASSERT_TRUE(projected->canShoot());
	state->removeUnit(first->unitId());
	ASSERT_TRUE(state->battleGetUnitByID(second->unitId())->alive());
	EXPECT_FALSE(projected->canShoot()); // Existing first-cart rule, not any-live-cart semantics.
	EXPECT_TRUE(shooter->canShoot());
}

TEST_F(FocusFireAITest, CopiedShotCacheTracksDestinationShooterAbility)
{
	ASSERT_NO_FATAL_FAILURE(prepareAI());
	shooter->shots.use(1);
	addStack(BattleSide::ATTACKER, creatureByName("core:ammoCart"),
		BattleHex(leftHex + 2 * GameConstants::BFIELD_WIDTH), 1);
	auto state = model();
	auto projected = state->getForUpdate(shooter->unitId());
	ASSERT_TRUE(projected->canShoot());
	const Bonus ability(BonusDuration::ONE_BATTLE, BonusType::SHOOTER, BonusSource::OTHER, 1, BonusSourceID());
	state->removeUnitBonus(shooter->unitId(), {ability});
	ASSERT_FALSE(projected->hasBonusOfType(BonusType::SHOOTER));
	EXPECT_FALSE(projected->canShoot());
	EXPECT_TRUE(shooter->canShoot());
	*projected = static_cast<const battle::CUnitState &>(*shooter);
	EXPECT_FALSE(projected->canShoot()); // Copy usage without restoring the source's SHOOTER cache target.
	state->addUnitBonus(shooter->unitId(), {ability});
	EXPECT_TRUE(projected->canShoot());
}

TEST_F(FocusFireAITest, CopiedRetaliationCachesUseDestinationBonusesAndRememberRoundMaximum)
{
	ASSERT_NO_FATAL_FAILURE(prepareAI());
	auto state = model();
	auto projected = state->getForUpdate(target->unitId());
	ASSERT_EQ(projected->counterAttacks.total(), 1);
	const Bonus disabled(BonusDuration::ONE_BATTLE, BonusType::NO_RETALIATION, BonusSource::OTHER, 1, BonusSourceID());
	state->addUnitBonus(target->unitId(), {disabled});
	EXPECT_EQ(projected->counterAttacks.total(), 0);
	EXPECT_EQ(target->counterAttacks.total(), 1);
	state->removeUnitBonus(target->unitId(), {disabled});
	const Bonus unlimited(BonusDuration::ONE_BATTLE, BonusType::UNLIMITED_RETALIATIONS,
		BonusSource::OTHER, 1, BonusSourceID());
	state->addUnitBonus(target->unitId(), {unlimited});
	EXPECT_FALSE(projected->counterAttacks.isLimited());
	EXPECT_TRUE(target->counterAttacks.isLimited());
	state->removeUnitBonus(target->unitId(), {unlimited});
	EXPECT_TRUE(projected->counterAttacks.isLimited());
	const Bonus extra(BonusDuration::ONE_BATTLE, BonusType::ADDITIONAL_RETALIATION,
		BonusSource::OTHER, 2, BonusSourceID());
	state->addUnitBonus(target->unitId(), {extra});
	ASSERT_EQ(projected->counterAttacks.total(), 3);
	state->removeUnitBonus(target->unitId(), {extra});
	ASSERT_EQ(projected->counterAttacks.total(), 3);
	auto nextModel = std::make_shared<HypotheticBattle>(environment.get(), state);
	auto copy = nextModel->getForUpdate(target->unitId());
	EXPECT_EQ(copy->counterAttacks.total(), 3);
	nextModel->nextRound();
	EXPECT_EQ(copy->counterAttacks.total(), 1);
	EXPECT_EQ(projected->counterAttacks.total(), 3);
}

TEST_F(FocusFireAITest, StateConstructorWarmCachesSelfCopyAndDetachedCopiesKeepOwnEnvironment)
{
	ASSERT_NO_FATAL_FAILURE(prepareAI());
	shooter->shots.use(1);
	auto * cart = addStack(BattleSide::ATTACKER, creatureByName("core:ammoCart"),
		BattleHex(leftHex + 2 * GameConstants::BFIELD_WIDTH), 1);
	ASSERT_TRUE(shooter->canShoot()); // Warm the source caches before copying.
	auto state = model();
	auto copy = std::make_shared<StackWithBonuses>(state.get(), static_cast<const battle::CUnitState *>(shooter));
	ASSERT_TRUE(copy->canShoot()); // Exercise the CUnitState overload, not just the Unit overload.
	state->removeUnit(cart->unitId());
	EXPECT_FALSE(copy->canShoot());
	EXPECT_TRUE(shooter->canShoot());
	*copy = static_cast<const battle::CUnitState &>(*shooter); // Reassign a warm destination.
	EXPECT_FALSE(copy->canShoot());
	const Bonus extra(BonusDuration::ONE_BATTLE, BonusType::ADDITIONAL_RETALIATION,
		BonusSource::OTHER, 2, BonusSourceID());
	copy->addUnitBonus({extra});
	ASSERT_EQ(copy->counterAttacks.total(), 3);
	copy->counterAttacks.use(1);
	*copy = static_cast<const battle::CUnitState &>(*copy);
	EXPECT_FALSE(copy->canShoot());
	EXPECT_EQ(copy->counterAttacks.available(), 2);
	const auto detachedUnit = copy->acquire();
	const auto detachedState = copy->acquireState();
	EXPECT_FALSE(detachedUnit->canShoot());
	EXPECT_FALSE(detachedState->canShoot());
	EXPECT_EQ(detachedState->counterAttacks.available(), 2);
	copy->removeUnitBonus(std::vector<Bonus>{extra});
	EXPECT_EQ(copy->counterAttacks.total(), 3);
	copy->afterNewRound();
	EXPECT_EQ(copy->counterAttacks.total(), 1);
	EXPECT_EQ(copy->counterAttacks.available(), 1);
}

TEST_F(FocusFireAITest, FollowUpShotScoresCurrentVictimHealthAndAuthoritySpendsBothShots)
{
	ASSERT_NO_FATAL_FAILURE(prepareAI());
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SHOTS, BonusSource::OTHER, 1, BonusSourceID()));
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID(), BonusCustomSubtype::damageTypeRanged));
	ASSERT_EQ(shooter->shots.available(), 2);
	ASSERT_EQ(shooter->getTotalAttacks(true), 2);
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	auto state = model();
	DamageCache cache;
	cache.buildDamageCache(state, BattleSide::ATTACKER);
	auto attackerState = shooter->acquireState();
	auto victimState = target->acquireState();
	const auto staleRepeated = 2 * AttackPossibility::calculateDamageReduce(shooter, target, 180, cache, state);
	float expected = 0;
	for(int shot = 0; shot < 2; ++shot)
	{
		expected += AttackPossibility::calculateDamageReduce(attackerState.get(), victimState.get(), 180, cache, state);
		int64_t damage = 180;
		victimState->damage(damage);
		attackerState->afterAttack(true, false);
	}
	ASSERT_GT(expected, staleRepeated); // Second shot kills across the first shot's partial-health boundary.
	BattleAttackInfo info(shooter, target, 0, true);
	const auto prediction = AttackPossibility::evaluate(info, shooter->getPosition(), cache, state);
	EXPECT_FLOAT_EQ(prediction.defenderDamageReduce, expected);
	ASSERT_NE(prediction.attackerState, nullptr);
	EXPECT_EQ(prediction.attackerState->shots.available(), 0);
	unsigned matched = 0;
	for(const auto & victim : prediction.affectedUnits)
		if(victim->unitId() == target->unitId())
		{
			++matched;
			EXPECT_EQ(victim->getAvailableHealth(), victimState->getAvailableHealth());
		}
	EXPECT_EQ(matched, 1u);
	const auto health = target->getAvailableHealth();
	ASSERT_TRUE(submit(BattleAction::makeShotAttack(shooter, target)));
	EXPECT_EQ(health - target->getAvailableHealth(), 360);
	EXPECT_EQ(shooter->shots.available(), 0);
}

TEST_F(FocusFireAITest, HostileOnlyAreaTargetsMatchAuthorityAndFollowCurrentControl)
{
	ASSERT_NO_FATAL_FAILURE(prepareAI());
	auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(rightHex + 5), 100);
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SHOOTS_ALL_ADJACENT, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	const auto authoritative = battle()->getAttackedCreatures(shooter, target->getPosition(), true);
	ASSERT_EQ(authoritative.first.size(), 1u);
	ASSERT_EQ(authoritative.first.front()->unitId(), target->unitId());
	auto state = model();
	const auto affected = [&]()
	{
		const auto * currentShooter = state->battleGetUnitByID(shooter->unitId());
		const auto * currentTarget = state->battleGetUnitByID(target->unitId());
		return state->getAttackedBattleUnits(currentShooter, currentTarget, currentTarget->getPosition(), true,
			currentShooter->getPosition(), currentTarget->getPosition());
	};
	ASSERT_EQ(affected().size(), 1u);
	EXPECT_EQ(affected().front()->unitId(), target->unitId());
	const Bonus control(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	state->addUnitBonus(ally->unitId(), {control});
	EXPECT_EQ(affected().size(), 2u); // Current hostile controller, not original army side.
	state->removeUnitBonus(ally->unitId(), {control});
	EXPECT_EQ(affected().size(), 1u);
	state->addUnitBonus(target->unitId(), {control});
	EXPECT_TRUE(affected().empty()); // Both victims currently friendly, despite the target's enemy origin.
	state->addUnitBonus(shooter->unitId(), {control});
	EXPECT_EQ(affected().size(), 2u); // Attacker controller also participates in the comparison.
	state->removeUnitBonus(shooter->unitId(), {control});
	state->removeUnitBonus(target->unitId(), {control});
	EXPECT_EQ(affected().size(), 1u);

	// The local hostile-only fix must not filter friendly-fire breath positions.
	const Bonus breath(BonusDuration::ONE_BATTLE, BonusType::TWO_HEX_ATTACK_BREATH,
		BonusSource::OTHER, 1, BonusSourceID());
	state->addUnitBonus(shooter->unitId(), {breath});
	const auto breathVictims = [&]()
	{
		return state->getAttackedBattleUnits(state->battleGetUnitByID(shooter->unitId()),
			state->battleGetUnitByID(target->unitId()), target->getPosition(), false,
			BattleHex(rightHex + 3), target->getPosition());
	};
	for(const bool flip : {false, true})
	{
		SCOPED_TRACE(flip);
		if(flip)
			state->addUnitBonus(ally->unitId(), {control});
		const auto victims = breathVictims();
		ASSERT_EQ(victims.size(), 1u);
		EXPECT_EQ(victims.front()->unitId(), ally->unitId());
	}
	state->removeUnitBonus(ally->unitId(), {control});
	state->removeUnitBonus(shooter->unitId(), {breath});
	DamageCache cache;
	cache.buildDamageCache(state, BattleSide::ATTACKER);
	BattleAttackInfo info(shooter, target, 0, true);
	const auto prediction = AttackPossibility::evaluate(info, shooter->getPosition(), cache, state);
	for(const auto & victim : prediction.affectedUnits)
		EXPECT_NE(victim->unitId(), ally->unitId());
	const auto allyHealth = ally->getAvailableHealth();
	const auto targetHealth = target->getAvailableHealth();
	ASSERT_TRUE(submit(BattleAction::makeShotAttack(shooter, target)));
	EXPECT_EQ(ally->getAvailableHealth(), allyHealth);
	EXPECT_EQ(targetHealth - target->getAvailableHealth(), 180);
}

TEST_F(FocusFireAITest, PerVictimPredictionAndServerSpendOneLastShotNotOnePerVictim)
{
	ASSERT_NO_FATAL_FAILURE(prepareAI());
	auto * collateral = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 5), 100);
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SHOOTS_ALL_ADJACENT, BonusSource::OTHER, 1, BonusSourceID()));
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID(), BonusCustomSubtype::damageTypeRanged));
	ASSERT_EQ(shooter->getTotalAttacks(true), 2);
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	auto state = model();
	DamageCache cache;
	cache.buildDamageCache(state, BattleSide::ATTACKER);
	BattleAttackInfo info(shooter, target, 0, true);
	const auto prediction = AttackPossibility::evaluate(info, shooter->getPosition(), cache, state);
	ASSERT_NE(prediction.attackerState, nullptr);
	EXPECT_EQ(prediction.attackerState->shots.available(), 0);
	unsigned predictedVictims = 0;
	for(const auto & victim : prediction.affectedUnits)
	{
		if(victim->unitId() == target->unitId() || victim->unitId() == collateral->unitId())
		{
			++predictedVictims;
			const int expected = victim->unitId() == target->unitId() ? 180 : 150;
			const auto * original = battle()->battleGetUnitByID(victim->unitId());
			EXPECT_EQ(original->getAvailableHealth() - victim->getAvailableHealth(), expected);
		}
	}
	EXPECT_EQ(predictedVictims, 2u);
	const auto before = server.attacks.size();
	ASSERT_TRUE(submit(BattleAction::makeShotAttack(shooter, target)));
	unsigned shots = 0;
	unsigned victims = 0;
	for(size_t i = before; i < server.attacks.size(); ++i)
	{
		const auto & attack = server.attacks[i];
		if(attack.stackAttacking != shooter->unitId() || !attack.shot() || attack.counter())
			continue;
		++shots;
		for(const auto & victim : attack.bsa)
		{
			if(victim.stackAttacked == target->unitId() || victim.stackAttacked == collateral->unitId())
			{
				++victims;
				EXPECT_EQ(victim.damageAmount, victim.stackAttacked == target->unitId() ? 180 : 150);
			}
		}
	}
	EXPECT_EQ(shots, 1u);
	EXPECT_EQ(victims, 2u);
	EXPECT_EQ(shooter->shots.available(), 0);
}

TEST_F(FocusFireAITest, MarkedCollateralDoesNotGetPrimaryPremium)
{
	ASSERT_NO_FATAL_FAILURE(prepareAI());
	auto * other = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 5), 100);
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SHOOTS_ALL_ADJACENT, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	auto state = model();
	DamageCache cache;
	cache.buildDamageCache(state, BattleSide::ATTACKER);
	const auto prediction = AttackPossibility::evaluate(
		BattleAttackInfo(shooter, other, 0, true), shooter->getPosition(), cache, state);
	unsigned checked = 0;
	for(const auto & victim : prediction.affectedUnits)
	{
		if(victim->unitId() == target->unitId())
		{
			++checked;
			EXPECT_EQ(target->getAvailableHealth() - victim->getAvailableHealth(), 150);
		}
	}
	EXPECT_EQ(checked, 1u);
}

TEST_F(FocusFireAITest, RealEvaluatorSubmitsTargetedCommandThatAuthorityAccepts)
{
	// Strong authored premium and two100-angel armies make routing decisive, not a balance assertion.
	focusBase = 200;
	shooterCount = 100;
	ASSERT_NO_FATAL_FAILURE(prepareAI());
	ASSERT_FALSE(attackerSideHero->hasSpellbook());
	BattleEvaluator evaluator(environment, callback, shooter, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(shooter);
	ASSERT_TRUE(evaluator.canCastSpell());
	ASSERT_TRUE(evaluator.attemptCastingSpell(shooter));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto action = callback->submitted.front();
	ASSERT_EQ(action.command, HeroCommand::FOCUS_FIRE);
	ASSERT_EQ(action.target.size(), 1u);
	ASSERT_TRUE(battle()->battleCanConfirmHeroCommand(action.side, action.command, action.target.front().unitValue));
	ASSERT_TRUE(submit(action));
	const auto mark = battle()->battleGetFocusFireState(BattleSide::ATTACKER);
	ASSERT_TRUE(mark);
	EXPECT_EQ(mark->targetUnitId, action.target.front().unitValue);
	EXPECT_TRUE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
}

TEST_F(FocusFireAITest, AlreadyActedShootersDoNotMakeAIBuyAnUnusableRoundPremium)
{
	focusBase = 200;
	ASSERT_NO_FATAL_FAILURE(prepareAI());
	shooter->movedThisRound = true;
	auto * melee = addStack(BattleSide::ATTACKER, creatureByName("core:angel"),
		BattleHex(leftHex + 2 * GameConstants::BFIELD_WIDTH), 1);
	activate(melee);
	ASSERT_TRUE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE));
	BattleEvaluator evaluator(environment, callback, melee, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(melee);
	evaluator.attemptCastingSpell(melee);
	for(const auto & action : callback->submitted)
		EXPECT_NE(action.command, HeroCommand::FOCUS_FIRE);
}

TEST_F(FocusFireAITest, StrongerLegalSpellCompetesWithTargetedOrderAndAuthorityAcceptsIt)
{
	focusBase = 200;
	ASSERT_NO_FATAL_FAILURE(prepareAI());
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::IMPLOSION);
	attackerSideHero->mana = 1000;
	const auto * spell = SpellID(SpellID::IMPLOSION).toSpell();
	const auto divisor = attackerSideHero->getEffectPowerDivisor(spell);
	ASSERT_GT(divisor, 0);
	const int64_t rating = int64_t{99} * divisor;
	ASSERT_LE(rating, std::numeric_limits<int32_t>::max());
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, static_cast<int32_t>(rating), ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(attackerSideHero->getEffectPower(spell), rating);
	ASSERT_TRUE(spell->canBeCast(callback->getBattle(BattleID(0)).get(), spells::Mode::HERO, attackerSideHero));
	ASSERT_GT(spell->calculateDamage(attackerSideHero), 1400);
	ASSERT_LT(spell->calculateDamage(attackerSideHero), target->getAvailableHealth());
	BattleEvaluator evaluator(environment, callback, shooter, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(shooter);
	ASSERT_TRUE(evaluator.attemptCastingSpell(shooter));
	ASSERT_EQ(callback->submitted.size(), 1u);
	ASSERT_EQ(callback->submitted.front().actionType, EActionType::HERO_SPELL);
	ASSERT_TRUE(submit(callback->submitted.front()));
	EXPECT_FALSE(battle()->battleGetFocusFireState(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 1);
}
