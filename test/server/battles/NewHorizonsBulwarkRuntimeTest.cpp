/*
 * NewHorizonsBulwarkRuntimeTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/battle/NewHorizonsBulwark.h"
#include "../../../lib/filesystem/ResourcePath.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

namespace
{
bool selectBulwarkPerkIfActive(CGHeroInstance * hero, std::string_view perkId)
{
	const std::string skillId(newHorizonsBulwark::SKILL_ID);
	const std::string perkIdentifier(perkId);
	if(hero->hasActivePerk(skillId, perkIdentifier))
		return true;
	const auto & perks = hero->getPerkState().rules["skills"][skillId]["perks"].Vector();
	const auto tierOf = [](const JsonNode & perk)
	{
		const auto required = perk["requires"].String();
		if(required == "basic")
			return 1;
		if(required == "advanced")
			return 2;
		if(required == "expert")
			return 3;
		return 0;
	};
	const auto definition = std::find_if(perks.begin(), perks.end(), [&](const JsonNode & perk)
	{
		return perk["id"].String() == perkIdentifier;
	});
	if(definition == perks.end() || (*definition)["effect"]["status"].String() != "active")
		return false;

	for(int requiredTier = 1; requiredTier < tierOf(*definition); ++requiredTier)
	{
		const bool tierAlreadySelected = std::any_of(hero->getPerkState().selected.begin(),
			hero->getPerkState().selected.end(), [&](const auto & selection)
		{
			if(selection.skillId != skillId)
				return false;
			const auto selectedDefinition = std::find_if(perks.begin(), perks.end(), [&](const JsonNode & perk)
			{
				return perk["id"].String() == selection.perkId;
			});
			return selectedDefinition != perks.end() && tierOf(*selectedDefinition) == requiredTier;
		});
		if(tierAlreadySelected)
			continue;

		const auto prerequisite = std::find_if(perks.begin(), perks.end(), [&](const JsonNode & perk)
		{
			return tierOf(perk) == requiredTier && perk["effect"]["status"].String() == "active";
		});
		if(prerequisite == perks.end())
			return false;
		hero->applyPerkSelection({skillId, (*prerequisite)["id"].String()});
	}

	hero->applyPerkSelection({skillId, perkIdentifier});
	return hero->hasActivePerk(skillId, perkIdentifier);
}

struct BulwarkReductionCase
{
	int rank;
	int basisPoints;
};

class NewHorizonsBulwarkRuntimeTest : public BattleTestFixture,
	public ::testing::WithParamInterface<BulwarkReductionCase>
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	SecondarySkill bulwark() const
	{
		const int decoded = SecondarySkill::decode("new-horizons:bulwarkOfTheMire");
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	std::pair<int64_t, int64_t> damageBeforeAndWhileDefending(int rank)
	{
		startGame();
		attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 20, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 20, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setSecSkillLevel(bulwark(), rank, ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(defenderSideHero->getSecSkillLevel(bulwark()), rank);
		EXPECT_EQ(defenderSideHero->getPerkSkillRank(std::string(newHorizonsBulwark::SKILL_ID)), rank);
		EXPECT_EQ(newHorizonsBulwark::rank(defenderSideHero), rank);
		startBattle();
		EXPECT_EQ(battle()->getSideHero(BattleSide::DEFENDER), defenderSideHero);
		EXPECT_EQ(newHorizonsBulwark::rank(battle()->getSideHero(BattleSide::DEFENDER)), rank);

		auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"),
			BattleHex(leftHex), 100);
		auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"),
			BattleHex(rightHex), 100);
		forceMaximumDamage(attacker);
		const BattleAttackInfo info(attacker, defender, 0, false);
		const auto before = battle()->calculateDmgRange(info).damage.max;
		defender->defending = true;
		EXPECT_TRUE(defender->defended());
		EXPECT_EQ(battle()->battleGetOwnerHero(defender), defenderSideHero);
		const auto whileDefending = battle()->calculateDmgRange(info).damage.max;
		return {before, whileDefending};
	}
};

class NewHorizonsBulwarkPerkRuntimeTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	SecondarySkill bulwark() const
	{
		const int decoded = SecondarySkill::decode(std::string(newHorizonsBulwark::SKILL_ID));
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void makeAutomaticallyControlled(CStack * stack)
	{
		stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::CPU_CONTROLLED, BonusSource::OTHER, 1, BonusSourceID()));
	}

	bool automaticActivationRecorded(const CStack * stack) const
	{
		return std::any_of(server.stackActivations.begin(), server.stackActivations.end(), [stack](const auto & activation)
		{
			return activation.battleID == BattleID(0) && activation.stack == stack->unitId()
				&& activation.reason == BattleUnitTurnReason::AUTOMATIC_ACTION;
		});
	}

	bool advanceUntilAutomaticActivation(const CStack * stack)
	{
		const auto maximumTurns = battle()->stacks.size() * 2;
		for(size_t turn = 0; turn < maximumTurns && !automaticActivationRecorded(stack); ++turn)
		{
			const auto * active = battle()->battleActiveUnit();
			if(!active || active->unitId() == stack->unitId())
				return false;
			if(!gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(active->unitSide()),
				BattleAction::makeDefend(active)))
				return false;
		}
		return automaticActivationRecorded(stack);
	}

	void setAvailableHealth(CStack * stack, int64_t desiredHealth)
	{
		auto state = stack->acquireState();
		ASSERT_GE(desiredHealth, 0);
		ASSERT_LE(desiredHealth, state->getAvailableHealth());
		int64_t damage = state->getAvailableHealth() - desiredHealth;
		state->damage(damage);
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		UnitChanges change(stack->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		change.healthDelta = -damage;
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
	}
};

class NewHorizonsBulwarkMirebornRuntimeTest : public BattleTestFixture,
	public ::testing::WithParamInterface<TerrainId>
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	SecondarySkill bulwark() const
	{
		const int decoded = SecondarySkill::decode(std::string(newHorizonsBulwark::SKILL_ID));
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void setAvailableHealth(CStack * stack, int64_t desiredHealth)
	{
		auto state = stack->acquireState();
		int64_t damage = state->getAvailableHealth() - desiredHealth;
		state->damage(damage);
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		UnitChanges change(stack->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		change.healthDelta = -damage;
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
	}
};
}

TEST_P(NewHorizonsBulwarkRuntimeTest, DefendingPhysicalStackUsesExactRankReduction)
{
	const auto [before, after] = damageBeforeAndWhileDefending(GetParam().rank);
	ASSERT_GT(before, 0);
	EXPECT_EQ(after, before * (10000 - GetParam().basisPoints) / 10000);
}

TEST_F(NewHorizonsBulwarkRuntimeTest, FirstMeleeAttackTriggersOneScaledPreemptiveStrikeWithoutSpendingRetaliation)
{
	startGame();
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 100);
	forceMaximumDamage(defender);
	blockRetaliation(attacker);
	defender->defending = true;

	const BattleAttackInfo normal(defender, attacker, 0, false);
	const auto normalDamage = battle()->calculateDmgRange(normal).damage.max;
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	ASSERT_GE(server.attacks.size(), 2u);
	EXPECT_EQ(server.attacks.front().stackAttacking, defender->unitId());
	ASSERT_EQ(server.attacks.front().bsa.size(), 1u);
	EXPECT_EQ(server.attacks.front().bsa.front().damageAmount, normalDamage * 50 / 100);
	EXPECT_FALSE(server.attacks.front().counter());
	EXPECT_EQ(defender->counterAttacks.total(), 1);
	EXPECT_TRUE(defender->bulwarkPreemptiveUsed);

	const auto defenderAttacks = std::count_if(server.attacks.begin(), server.attacks.end(), [&](const auto & result)
	{
		return result.stackAttacking == defender->unitId();
	});
	battle()->activeStack = attacker->unitId();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	EXPECT_EQ(std::count_if(server.attacks.begin(), server.attacks.end(), [&](const auto & result)
	{
		return result.stackAttacking == defender->unitId();
	}), defenderAttacks);
}

TEST_F(NewHorizonsBulwarkRuntimeTest, MeleeMagogAttackStillTriggersBulwarkPreemptiveStrike)
{
	startGame();
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	startBattle();
	auto * magog = addStack(BattleSide::ATTACKER, creatureByName("core:magog"), BattleHex(80), 100);
	// Keep the pre-emptive hit nonlethal so the incoming Magog melee attack resolves too.
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 1);
	ASSERT_TRUE(magog->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
	const BattleAttackInfo magogMelee(magog, defender, 0, false);
	const auto unmitigatedDamage = battle()->calculateDmgRange(magogMelee).damage.max;
	defender->defending = true;
	const auto defendedDamage = battle()->calculateDmgRange(magogMelee).damage.max;
	EXPECT_EQ(defendedDamage, unmitigatedDamage * 95 / 100);
	forceMaximumDamage(defender);
	blockRetaliation(magog);

	const BattleAttackInfo normal(defender, magog, 0, false);
	const auto normalDamage = battle()->calculateDmgRange(normal).damage.max;
	ASSERT_GT(magog->getAvailableHealth(), normalDamage * 50 / 100);
	ASSERT_TRUE(attack(magog, defender->getPosition()));
	ASSERT_GE(server.attacks.size(), 2u);
	EXPECT_EQ(server.attacks.front().stackAttacking, defender->unitId());
	ASSERT_EQ(server.attacks.front().bsa.size(), 1u);
	EXPECT_EQ(server.attacks.front().bsa.front().damageAmount, normalDamage * 50 / 100);
	EXPECT_TRUE(defender->bulwarkPreemptiveUsed);
}

TEST_F(NewHorizonsBulwarkRuntimeTest, AdvancedReflectsActualReducedMeleeDamageWithoutAnotherAttack)
{
	startGame();
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 100);
	forceMaximumDamage(attacker);
	blockRetaliation(attacker);
	defender->defending = true;
	defender->bulwarkPreemptiveUsed = true;
	const auto attackerHealth = attacker->getAvailableHealth();
	const auto defenderHealth = defender->getAvailableHealth();

	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	ASSERT_EQ(server.attacks.size(), 1u);
	ASSERT_EQ(server.attacks.front().bsa.size(), 1u);
	const int64_t received = std::min<int64_t>(server.attacks.front().bsa.front().damageAmount, defenderHealth);
	EXPECT_EQ(attackerHealth - attacker->getAvailableHealth(), received * 25 / 100);
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, VengefulMireReflectsSeventyFivePercentOfActualExpertMeleeDamage)
{
	startGame();
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(selectBulwarkPerkIfActive(defenderSideHero, newHorizonsBulwark::VENGEFUL_MIRE_ID));
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 100);
	forceMaximumDamage(attacker);
	blockRetaliation(attacker);
	defender->defending = true;
	defender->bulwarkPreemptiveUsed = true;
	const auto attackerHealthBefore = attacker->getAvailableHealth();
	const auto defenderHealthBefore = defender->getAvailableHealth();

	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	ASSERT_EQ(server.attacks.size(), 1u);
	ASSERT_EQ(server.attacks.front().bsa.size(), 1u);
	const int64_t actualLoss = std::min<int64_t>(server.attacks.front().bsa.front().damageAmount, defenderHealthBefore);
	EXPECT_EQ(attackerHealthBefore - attacker->getAvailableHealth(),
		newHorizonsBulwark::reflectedDamage(actualLoss, 7500));
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, ToxicSpinesPoisonsTheFirstReflectedMeleeAttackerPerStackAndRound)
{
	startGame();
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(selectBulwarkPerkIfActive(defenderSideHero, newHorizonsBulwark::TOXIC_SPINES_ID));
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 100);
	forceMaximumDamage(attacker);
	blockRetaliation(attacker);
	blockRetaliation(defender);
	const auto attackerHealthBefore = attacker->getAvailableHealth();
	beginCombat();
	defender->defending = true;
	defender->bulwarkPreemptiveUsed = true;

	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	const auto actualReflectedLoss = attackerHealthBefore - attacker->getAvailableHealth();
	ASSERT_GT(actualReflectedLoss, 0);
	const auto poisonBase = newHorizonsBulwark::toxicSpinesPoisonBase(actualReflectedLoss);
	EXPECT_EQ(attacker->physicalPoisonBaseDamage, poisonBase);
	EXPECT_EQ(attacker->physicalPoisonActivationsRemaining, 3);
	EXPECT_EQ(attacker->physicalPoisonSourceStackId, static_cast<int32_t>(defender->unitId()));
	EXPECT_EQ(defender->bulwarkToxicSpinesRound, battle()->getRound());

	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	EXPECT_EQ(attacker->physicalPoisonBaseDamage, poisonBase);
	EXPECT_EQ(attacker->physicalPoisonActivationsRemaining, 3);
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, PhysicalPoisonRefreshesOnlyForEqualOrGreaterPotencyAndEscalatesThreeTicks)
{
	startGame();
	startBattle();
	auto * stack = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 10);
	auto state = stack->acquireState();
	EXPECT_TRUE(newHorizonsBulwark::applyPhysicalPoison(state.get(), 5, 7));
	EXPECT_EQ(newHorizonsBulwark::physicalPoisonTickDamage(state.get()), 5);
	newHorizonsBulwark::advancePhysicalPoison(state.get());
	EXPECT_EQ(newHorizonsBulwark::physicalPoisonTickDamage(state.get()), 7);
	EXPECT_FALSE(newHorizonsBulwark::applyPhysicalPoison(state.get(), 4, 8));
	EXPECT_EQ(state->physicalPoisonActivationsRemaining, 2);
	EXPECT_TRUE(newHorizonsBulwark::applyPhysicalPoison(state.get(), 5, 9));
	EXPECT_EQ(state->physicalPoisonActivationsRemaining, 3);
	EXPECT_EQ(state->physicalPoisonSourceStackId, 9);
	newHorizonsBulwark::advancePhysicalPoison(state.get());
	newHorizonsBulwark::advancePhysicalPoison(state.get());
	EXPECT_EQ(newHorizonsBulwark::physicalPoisonTickDamage(state.get()), 10);
	newHorizonsBulwark::advancePhysicalPoison(state.get());
	EXPECT_EQ(state->physicalPoisonBaseDamage, 0);
	EXPECT_EQ(state->physicalPoisonSourceStackId, -1);

	EXPECT_TRUE(newHorizonsBulwark::applyPhysicalPoison(state.get(), 10, 4));
	EXPECT_TRUE(newHorizonsBulwark::applyPhysicalPoison(state.get(), 20, 5));
	EXPECT_EQ(state->physicalPoisonBaseDamage, 20);
	EXPECT_EQ(state->physicalPoisonActivationsRemaining, 3);
	newHorizonsBulwark::clearPhysicalPoison(state.get());
	EXPECT_EQ(newHorizonsBulwark::physicalPoisonTickDamage(state.get()), 0);
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, PhysicalPoisonTicksOnTheNextRealStackActivation)
{
	startGame();
	startBattle();
	auto * poisoned = addStack(BattleSide::ATTACKER, creatureByName("core:phoenix"), BattleHex(leftHex), 10);
	const auto healthBefore = poisoned->getAvailableHealth();
	auto state = poisoned->acquireState();
	ASSERT_TRUE(newHorizonsBulwark::applyPhysicalPoison(state.get(), 7,
		static_cast<int32_t>(poisoned->unitId())));
	BattleUnitsChanged update;
	update.battleID = BattleID(0);
	UnitChanges change(poisoned->unitId(), UnitChanges::EOperation::UPDATE);
	change.data = state->save();
	update.changedStacks.push_back(std::move(change));
	gameHandler->sendAndApply(update);

	beginCombat();
	EXPECT_EQ(poisoned->getAvailableHealth(), healthBefore - 7);
	EXPECT_EQ(poisoned->physicalPoisonBaseDamage, 7);
	EXPECT_EQ(poisoned->physicalPoisonActivationsRemaining, 2);
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, PhysicalPoisonTicksOnAnAutomaticCreatureActivation)
{
	startGame();
	startBattle();
	auto * poisoned = addStack(BattleSide::ATTACKER, creatureByName("core:phoenix"), BattleHex(25), 10);
	addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(150), 1000);
	makeAutomaticallyControlled(poisoned);
	const auto healthBefore = poisoned->getAvailableHealth();
	auto state = poisoned->acquireState();
	ASSERT_TRUE(newHorizonsBulwark::applyPhysicalPoison(state.get(), 7,
		static_cast<int32_t>(poisoned->unitId())));
	BattleUnitsChanged update;
	update.battleID = BattleID(0);
	UnitChanges change(poisoned->unitId(), UnitChanges::EOperation::UPDATE);
	change.data = state->save();
	update.changedStacks.push_back(std::move(change));
	gameHandler->sendAndApply(update);

	beginCombat();
	ASSERT_TRUE(advanceUntilAutomaticActivation(poisoned));
	EXPECT_LT(poisoned->getAvailableHealth(), healthBefore);
	EXPECT_EQ(poisoned->physicalPoisonBaseDamage, 7);
	EXPECT_EQ(poisoned->physicalPoisonActivationsRemaining, 2);
	EXPECT_TRUE(std::ranges::any_of(server.injuries, [poisoned](const StacksInjured & injury)
	{
		return std::ranges::any_of(injury.stacks, [poisoned](const BattleStackAttacked & hit)
		{
			return hit.stackAttacked == poisoned->unitId() && hit.damageAmount == 7;
		});
	}));
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, LethalPhysicalPoisonStopsAnAutomaticCreatureAction)
{
	startGame();
	startBattle();
	auto * poisoned = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(25), 1);
	addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(150), 1000);
	makeAutomaticallyControlled(poisoned);
	setAvailableHealth(poisoned, 1);
	auto state = poisoned->acquireState();
	ASSERT_TRUE(newHorizonsBulwark::applyPhysicalPoison(state.get(), 5,
		static_cast<int32_t>(poisoned->unitId())));
	BattleUnitsChanged update;
	update.battleID = BattleID(0);
	UnitChanges change(poisoned->unitId(), UnitChanges::EOperation::UPDATE);
	change.data = state->save();
	update.changedStacks.push_back(std::move(change));
	gameHandler->sendAndApply(update);

	beginCombat();
	ASSERT_TRUE(advanceUntilAutomaticActivation(poisoned));
	EXPECT_FALSE(poisoned->alive());
	EXPECT_EQ(poisoned->physicalPoisonBaseDamage, 0);
	EXPECT_EQ(poisoned->physicalPoisonActivationsRemaining, 0);
	EXPECT_TRUE(std::none_of(server.startedActions.begin(), server.startedActions.end(), [poisoned](const auto & action)
	{
		return action.battleID == BattleID(0) && action.ba.stackNumber == poisoned->unitId();
	}));
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, LethalPhysicalPoisonTickSavesTheConsumedStatusWithTheDeath)
{
	startGame();
	startBattle();
	auto * poisoned = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 1);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(rightHex), 1);
	setAvailableHealth(poisoned, 1);
	auto state = poisoned->acquireState();
	ASSERT_TRUE(newHorizonsBulwark::applyPhysicalPoison(state.get(), 5, static_cast<int32_t>(enemy->unitId())));
	BattleUnitsChanged update;
	update.battleID = BattleID(0);
	UnitChanges change(poisoned->unitId(), UnitChanges::EOperation::UPDATE);
	change.data = state->save();
	update.changedStacks.push_back(std::move(change));
	gameHandler->sendAndApply(update);

	beginCombat();
	EXPECT_FALSE(poisoned->alive());
	EXPECT_EQ(poisoned->physicalPoisonBaseDamage, 0);
	EXPECT_EQ(poisoned->physicalPoisonActivationsRemaining, 0);
	ASSERT_FALSE(server.injuries.empty());
	const auto & finalInjury = server.injuries.back().stacks.front();
	EXPECT_EQ(finalInjury.stackAttacked, poisoned->unitId());
	EXPECT_EQ(finalInjury.damageAmount, 1);
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, ExpertReflectionUsesOnlyRemainingHealthOnLethalOverkill)
{
	startGame();
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 100);
	forceMaximumDamage(attacker);
	blockRetaliation(attacker);
	defender->defending = true;
	defender->bulwarkPreemptiveUsed = true;
	setAvailableHealth(defender, 7);
	const auto attackerHealthBefore = attacker->getAvailableHealth();
	const auto defenderHealthBefore = defender->getAvailableHealth();
	const BattleAttackInfo overkill(attacker, defender, 0, false);
	const auto potentialDamage = battle()->calculateDmgRange(overkill).damage.max;
	ASSERT_GT(potentialDamage, defenderHealthBefore);

	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	ASSERT_EQ(server.attacks.size(), 1u);
	ASSERT_EQ(server.attacks.front().bsa.size(), 1u);
	EXPECT_EQ(server.attacks.front().bsa.front().damageAmount, defenderHealthBefore);
	EXPECT_FALSE(defender->alive());
	EXPECT_EQ(attackerHealthBefore - attacker->getAvailableHealth(),
		newHorizonsBulwark::reflectedDamage(defenderHealthBefore, 5000));
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, SwampRenewalHealsSurvivingHealthWithoutRestoringCasualties)
{
	startGame();
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(selectBulwarkPerkIfActive(defenderSideHero, newHorizonsBulwark::SWAMP_RENEWAL_ID));
	startBattle();
	auto * stack = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	setAvailableHealth(stack, 1);
	auto state = stack->acquireState();
	const auto casualtiesBefore = stack->getCount();
	state->bulwarkDefendPhysicalDamage = 5000;
	state->afterNewRound(false);
	EXPECT_EQ(state->bulwarkDefendPhysicalDamage, 5000);

	const int64_t healed = newHorizonsBulwark::applySwampRenewal(state.get(), defenderSideHero);
	EXPECT_EQ(healed, stack->getMaxHealth() - 1);
	EXPECT_EQ(stack->getCount(), casualtiesBefore);
	EXPECT_EQ(state->bulwarkDefendPhysicalDamage, 0);
	EXPECT_EQ(state->getAvailableHealth(), stack->getMaxHealth());
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, PerkRuntimeStateDefaultsAndSurvivesBattleStateUpdateSerialization)
{
	startGame();
	startBattle();
	auto * stack = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 10);
	EXPECT_FALSE(stack->bulwarkMireGripApplied);
	EXPECT_EQ(stack->bulwarkDefendPhysicalDamage, 0);
	EXPECT_EQ(stack->bulwarkImmovableRound, -1);
	EXPECT_EQ(stack->bulwarkToxicSpinesRound, -1);
	EXPECT_EQ(stack->physicalPoisonBaseDamage, 0);
	EXPECT_EQ(stack->physicalPoisonActivationsRemaining, 0);
	EXPECT_EQ(stack->physicalPoisonSourceStackId, -1);
	auto state = stack->acquireState();
	state->bulwarkMireGripApplied = true;
	state->bulwarkDefendPhysicalDamage = 321;
	state->bulwarkImmovableRound = 4;
	state->bulwarkToxicSpinesRound = 4;
	state->physicalPoisonBaseDamage = 12;
	state->physicalPoisonActivationsRemaining = 2;
	state->physicalPoisonSourceStackId = 9;
	BattleUnitsChanged update;
	update.battleID = BattleID(0);
	UnitChanges change(stack->unitId(), UnitChanges::EOperation::UPDATE);
	change.data = state->save();
	update.changedStacks.push_back(std::move(change));
	gameHandler->sendAndApply(update);
	EXPECT_TRUE(stack->bulwarkMireGripApplied);
	EXPECT_EQ(stack->bulwarkDefendPhysicalDamage, 321);
	EXPECT_EQ(stack->bulwarkImmovableRound, 4);
	EXPECT_EQ(stack->bulwarkToxicSpinesRound, 4);
	EXPECT_EQ(stack->physicalPoisonBaseDamage, 12);
	EXPECT_EQ(stack->physicalPoisonActivationsRemaining, 2);
	EXPECT_EQ(stack->physicalPoisonSourceStackId, 9);
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, SwampRenewalPublishesItsHealOnTheNextAuthoritativeActivation)
{
	startGame();
	for(auto * hero : {attackerSideHero, defenderSideHero})
	{
		hero->setSecSkillLevel(bulwark(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(selectBulwarkPerkIfActive(hero, newHorizonsBulwark::SWAMP_RENEWAL_ID));
	}
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	for(auto * stack : {attacker, defender})
	{
		setAvailableHealth(stack, 1);
		auto state = stack->acquireState();
		state->bulwarkDefendPhysicalDamage = 10;
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		UnitChanges change(stack->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
	}

	beginCombat();
	const auto * active = battle()->battleActiveUnit();
	ASSERT_NE(active, nullptr);
	const auto * activatedStack = dynamic_cast<const CStack *>(active);
	ASSERT_NE(activatedStack, nullptr);
	EXPECT_EQ(activatedStack->getAvailableHealth(), 2);
	EXPECT_EQ(activatedStack->bulwarkDefendPhysicalDamage, 0);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Swamp Renewal restores") != std::string::npos;
	}));
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, SwampRenewalHealsAndLogsOnAnAutomaticCreatureActivation)
{
	startGame();
	attackerSideHero->setSecSkillLevel(bulwark(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(selectBulwarkPerkIfActive(attackerSideHero, newHorizonsBulwark::SWAMP_RENEWAL_ID));
	startBattle();
	auto * stack = addStack(BattleSide::ATTACKER, creatureByName("core:phoenix"), BattleHex(25), 10);
	addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(150), 1000);
	makeAutomaticallyControlled(stack);
	setAvailableHealth(stack, 1);
	auto state = stack->acquireState();
	state->bulwarkDefendPhysicalDamage = 20;
	BattleUnitsChanged update;
	update.battleID = BattleID(0);
	UnitChanges change(stack->unitId(), UnitChanges::EOperation::UPDATE);
	change.data = state->save();
	update.changedStacks.push_back(std::move(change));
	gameHandler->sendAndApply(update);

	const std::string expectedLog = "Swamp Renewal restores " + stack->getName() + " 2 Health.";
	beginCombat();
	ASSERT_TRUE(advanceUntilAutomaticActivation(stack));
	EXPECT_TRUE(stack->alive());
	EXPECT_EQ(stack->bulwarkDefendPhysicalDamage, 0);
	EXPECT_NE(std::ranges::find(server.battleLogLines, expectedLog), server.battleLogLines.end())
		<< ::testing::PrintToString(server.battleLogLines);
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, MireGripReducesMeleeAttackersSpeedUntilTheirNextActivation)
{
	startGame();
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(selectBulwarkPerkIfActive(defenderSideHero, newHorizonsBulwark::MIRE_GRIP_ID));
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 100);
	forceMaximumDamage(attacker);
	blockRetaliation(attacker);
	blockRetaliation(defender);
	defender->defending = true;
	defender->bulwarkPreemptiveUsed = true;
	const int speedBefore = attacker->valOfBonuses(BonusType::STACKS_SPEED);

	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	EXPECT_EQ(attacker->valOfBonuses(BonusType::STACKS_SPEED), speedBefore - 2);
	EXPECT_TRUE(attacker->bulwarkMireGripApplied);

	battle()->nextTurn(attacker->unitId(), BattleUnitTurnReason::HERO_SPELLCAST);
	EXPECT_EQ(attacker->valOfBonuses(BonusType::STACKS_SPEED), speedBefore - 2);
	EXPECT_TRUE(attacker->bulwarkMireGripApplied);
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, MireGripExpiresAtTheNextRealActivation)
{
	startGame();
	startBattle();
	auto * fastest = addStack(BattleSide::ATTACKER, creatureByName("core:phoenix"), BattleHex(leftHex), 10);
	const auto decoded = SecondarySkill::decode(std::string(newHorizonsBulwark::SKILL_ID));
	const Bonus penalty(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED, BonusSource::OTHER, -2,
		BonusSourceID(SecondarySkill(decoded)));
	fastest->addNewBonus(std::make_shared<Bonus>(penalty));
	fastest->bulwarkMireGripApplied = true;
	const int speedBeforeActivation = fastest->valOfBonuses(BonusType::STACKS_SPEED) + 2;
	BattleUnitsChanged update;
	update.battleID = BattleID(0);
	UnitChanges state(fastest->unitId(), UnitChanges::EOperation::UPDATE);
	state.data = fastest->acquireState()->save();
	update.changedStacks.push_back(std::move(state));
	gameHandler->sendAndApply(update);

	beginCombat();
	EXPECT_FALSE(fastest->bulwarkMireGripApplied);
	EXPECT_EQ(fastest->valOfBonuses(BonusType::STACKS_SPEED), speedBeforeActivation);
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, MireGripExpiresAtTheNextAutomaticCreatureActivation)
{
	startGame();
	attackerSideHero->setSecSkillLevel(bulwark(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(selectBulwarkPerkIfActive(attackerSideHero, newHorizonsBulwark::MIRE_GRIP_ID));
	startBattle();
	auto * stack = addStack(BattleSide::ATTACKER, creatureByName("core:phoenix"), BattleHex(25), 10);
	addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(150), 1000);
	makeAutomaticallyControlled(stack);
	const auto decoded = SecondarySkill::decode(std::string(newHorizonsBulwark::SKILL_ID));
	const Bonus penalty(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED, BonusSource::OTHER, -2,
		BonusSourceID(SecondarySkill(decoded)));
	stack->addNewBonus(std::make_shared<Bonus>(penalty));
	stack->bulwarkMireGripApplied = true;
	const int speedBeforeActivation = stack->valOfBonuses(BonusType::STACKS_SPEED) + 2;
	BattleUnitsChanged update;
	update.battleID = BattleID(0);
	UnitChanges change(stack->unitId(), UnitChanges::EOperation::UPDATE);
	change.data = stack->acquireState()->save();
	update.changedStacks.push_back(std::move(change));
	gameHandler->sendAndApply(update);

	beginCombat();
	ASSERT_TRUE(advanceUntilAutomaticActivation(stack));
	EXPECT_FALSE(stack->bulwarkMireGripApplied);
	EXPECT_EQ(stack->valOfBonuses(BonusType::STACKS_SPEED), speedBeforeActivation);
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, MireGripTriggersFromDamagingCollateralAgainstDefendingBulwark)
{
	startGame();
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(selectBulwarkPerkIfActive(defenderSideHero, newHorizonsBulwark::MIRE_GRIP_ID));
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:hydra"), BattleHex(leftHex), 10);
	auto * primary = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	auto * collateral = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(leftHex - 17), 100);
	forceMaximumDamage(attacker);
	blockRetaliation(attacker);
	blockRetaliation(primary);
	blockRetaliation(collateral);
	const auto speedBefore = attacker->valOfBonuses(BonusType::STACKS_SPEED);
	beginCombat();
	for(auto * target : {primary, collateral})
	{
		target->defending = true;
		target->bulwarkPreemptiveUsed = true;
	}

	ASSERT_TRUE(attack(attacker, primary->getPosition()));
	ASSERT_EQ(server.attacks.size(), 1u);
	ASSERT_EQ(server.attacks.front().bsa.size(), 2u);
	EXPECT_TRUE(std::ranges::any_of(server.attacks.front().bsa, [collateral](const BattleStackAttacked & hit)
	{
		return hit.stackAttacked == collateral->unitId() && hit.damageAmount > 0;
	}));
	EXPECT_TRUE(attacker->bulwarkMireGripApplied);
	EXPECT_EQ(attacker->valOfBonuses(BonusType::STACKS_SPEED), speedBefore - 2);
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, MireGripDoesNotTriggerOnRangedSpellLikeDamage)
{
	startGame();
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(selectBulwarkPerkIfActive(defenderSideHero, newHorizonsBulwark::MIRE_GRIP_ID));
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:magog"), BattleHex(70), 10);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(110), 100);
	ASSERT_TRUE(attacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
	forceMaximumDamage(attacker);
	blockRetaliation(attacker);
	blockRetaliation(defender);
	const auto speedBefore = attacker->valOfBonuses(BonusType::STACKS_SPEED);
	beginCombat();
	defender->defending = true;
	defender->bulwarkPreemptiveUsed = true;
	battle()->activeStack = attacker->unitId();

	BattleAction action = BattleAction::makeShotAttack(attacker, defender);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(attacker->unitSide()), action));
	EXPECT_FALSE(attacker->bulwarkMireGripApplied);
	EXPECT_EQ(attacker->valOfBonuses(BonusType::STACKS_SPEED), speedBefore);
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, ImmovableAndSwampRenewalIgnoreRangedSpellLikeDamage)
{
	startGame();
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(selectBulwarkPerkIfActive(defenderSideHero, newHorizonsBulwark::IMMOVABLE_ID));
	ASSERT_TRUE(selectBulwarkPerkIfActive(defenderSideHero, newHorizonsBulwark::SWAMP_RENEWAL_ID));
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:magog"), BattleHex(70), 10);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(110), 100);
	ASSERT_TRUE(attacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
	blockRetaliation(attacker);
	blockRetaliation(defender);
	beginCombat();
	defender->defending = true;
	defender->bulwarkPreemptiveUsed = true;
	battle()->activeStack = attacker->unitId();

	BattleAction action = BattleAction::makeShotAttack(attacker, defender);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(attacker->unitSide()), action));
	EXPECT_EQ(defender->bulwarkImmovableRound, -1);
	EXPECT_EQ(defender->bulwarkDefendPhysicalDamage, 0);
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, SharedCoverAddsHalfReductionOnlyWhenBothAdjacentStacksDefend)
{
	startGame();
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(selectBulwarkPerkIfActive(defenderSideHero, newHorizonsBulwark::SHARED_COVER_ID));
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 100);
	auto * coverSource = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(82), 100);
	forceMaximumDamage(attacker);
	coverSource->defending = true;
	target->defending = true;
	const BattleAttackInfo info(attacker, target, 0, false);
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	const auto withoutBulwark = battle()->calculateDmgRange(info).damage.max;
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	const int reduction = newHorizonsBulwark::reductionBasisPoints(
		MasteryLevel::ADVANCED, defenderSideHero->getPrimSkillLevel(PrimarySkill::DEFENSE), false);
	const int combined = std::min(10000, reduction + newHorizonsBulwark::sharedCoverBasisPoints(reduction));
	EXPECT_EQ(battle()->calculateDmgRange(info).damage.max, withoutBulwark * (10000 - combined) / 10000);

	coverSource->defending = false;
	EXPECT_EQ(battle()->calculateDmgRange(info).damage.max, withoutBulwark * (10000 - reduction) / 10000);
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, ImmovableReducesOnlyTheFirstPhysicalCreatureAttackWhileDefending)
{
	startGame();
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(selectBulwarkPerkIfActive(defenderSideHero, newHorizonsBulwark::IMMOVABLE_ID));
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 100);
	forceMaximumDamage(attacker);
	blockRetaliation(attacker);
	blockRetaliation(defender);
	defender->defending = true;
	defender->bulwarkPreemptiveUsed = true;
	const BattleAttackInfo info(attacker, defender, 0, false);
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	const auto withoutBulwark = battle()->calculateDmgRange(info).damage.max;
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	const int baseReduction = newHorizonsBulwark::reductionBasisPoints(
		MasteryLevel::EXPERT, defenderSideHero->getPrimSkillLevel(PrimarySkill::DEFENSE), false);
	const auto firstAttack = battle()->calculateDmgRange(info).damage.max;
	EXPECT_EQ(firstAttack, withoutBulwark * (10000 - baseReduction) / 10000 * 75 / 100);

	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	EXPECT_EQ(defender->bulwarkImmovableRound, battle()->getRound());
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	const auto withoutBulwarkOnSecondAttack = battle()->calculateDmgRange(info).damage.max;
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	const auto secondForecast = battle()->calculateDmgRange(info).damage.max;
	EXPECT_EQ(secondForecast, withoutBulwarkOnSecondAttack * (10000 - baseReduction) / 10000);
	const auto secondAttackIndex = server.attacks.size();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	ASSERT_GT(server.attacks.size(), secondAttackIndex);
	const auto secondAttack = server.attacks.back().bsa.front().damageAmount;
	EXPECT_EQ(secondAttack, secondForecast);
}

TEST_P(NewHorizonsBulwarkMirebornRuntimeTest, AddsFivePointsOnlyOnSwampAndRough)
{
	startGame();
	gameState()->getMap().getTile(int3(4, 4, 0)).terrainType = GetParam();
	defenderSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 20, ChangeValueMode::ABSOLUTE);
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	if(!selectBulwarkPerkIfActive(defenderSideHero, newHorizonsBulwark::MIREBORN_ID))
		GTEST_SKIP() << "Mireborn is not active in the New Horizons perk rules";

	startBattle();
	ASSERT_EQ(battle()->getTerrainType(), GetParam());
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	forceMaximumDamage(attacker);
	const BattleAttackInfo info(attacker, defender, 0, false);
	const auto before = battle()->calculateDmgRange(info).damage.max;
	defender->defending = true;

	const bool mireTerrain = GetParam() == TerrainId::SWAMP || GetParam() == TerrainId::ROUGH;
	const int expectedReduction = newHorizonsBulwark::reductionBasisPoints(1, 20, mireTerrain);
	const auto whileDefending = battle()->calculateDmgRange(info).damage.max;
	ASSERT_GT(before, 0);
	EXPECT_EQ(whileDefending, before * (10000 - expectedReduction) / 10000);
}

INSTANTIATE_TEST_SUITE_P(Terrain, NewHorizonsBulwarkMirebornRuntimeTest,
	::testing::Values(TerrainId::SWAMP, TerrainId::ROUGH, TerrainId::GRASS));

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, BogAmbushStrengthensOnlyItsFirstMeleePreemptiveStrike)
{
	startGame();
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	if(!selectBulwarkPerkIfActive(defenderSideHero, newHorizonsBulwark::BOG_AMBUSH_ID))
		GTEST_SKIP() << "Bog Ambush is not active in the New Horizons perk rules";
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 100);
	forceMaximumDamage(defender);
	blockRetaliation(attacker);
	defender->defending = true;

	const BattleAttackInfo normal(defender, attacker, 0, false);
	const auto normalDamage = battle()->calculateDmgRange(normal).damage.max;
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	ASSERT_GE(server.attacks.size(), 2u);
	EXPECT_EQ(server.attacks.front().stackAttacking, defender->unitId());
	ASSERT_EQ(server.attacks.front().bsa.size(), 1u);
	EXPECT_EQ(server.attacks.front().bsa.front().damageAmount, normalDamage * 75 / 100);
	EXPECT_TRUE(defender->bulwarkPreemptiveUsed);

	const auto defenderAttacks = std::count_if(server.attacks.begin(), server.attacks.end(), [&](const auto & result)
	{
		return result.stackAttacking == defender->unitId();
	});
	battle()->activeStack = attacker->unitId();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	EXPECT_EQ(std::count_if(server.attacks.begin(), server.attacks.end(), [&](const auto & result)
	{
		return result.stackAttacking == defender->unitId();
	}), defenderAttacks);
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, MirebornDoesNotAddReductionAgainstSiegeWeaponDamage)
{
	startGame();
	gameState()->getMap().getTile(int3(4, 4, 0)).terrainType = TerrainId::SWAMP;
	defenderSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 20, ChangeValueMode::ABSOLUTE);
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	if(!selectBulwarkPerkIfActive(defenderSideHero, newHorizonsBulwark::MIREBORN_ID))
		GTEST_SKIP() << "Mireborn is not active in the New Horizons perk rules";
	startBattle();
	auto * ballista = addStack(BattleSide::ATTACKER, creatureByName("core:ballista"), BattleHex(leftHex), 1);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	ASSERT_TRUE(ballista->hasBonusOfType(BonusType::SIEGE_WEAPON));
	defender->defending = true;
	const BattleAttackInfo info(ballista, defender, 0, true);
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	const auto withoutBulwark = battle()->calculateDmgRange(info).damage.max;
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	const auto withBulwark = battle()->calculateDmgRange(info).damage.max;
	EXPECT_EQ(withBulwark, withoutBulwark);
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, MirebornDoesNotReduceDamageToSiegeWeaponStacks)
{
	startGame();
	gameState()->getMap().getTile(int3(4, 4, 0)).terrainType = TerrainId::SWAMP;
	defenderSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 20, ChangeValueMode::ABSOLUTE);
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	if(!selectBulwarkPerkIfActive(defenderSideHero, newHorizonsBulwark::MIREBORN_ID))
		GTEST_SKIP() << "Mireborn is not active in the New Horizons perk rules";
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
	auto * ballista = addStack(BattleSide::DEFENDER, creatureByName("core:ballista"), BattleHex(rightHex), 1);
	ASSERT_TRUE(ballista->hasBonusOfType(BonusType::SIEGE_WEAPON));
	ballista->defending = true;
	const BattleAttackInfo info(attacker, ballista, 0, false);
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	const auto withoutBulwark = battle()->calculateDmgRange(info).damage.max;
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	const auto withBulwark = battle()->calculateDmgRange(info).damage.max;
	EXPECT_EQ(withBulwark, withoutBulwark);
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, ThickHideReflectsHalfActualDamageFromPhysicalRangedCreatureAttacks)
{
	startGame();
	defenderSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 20, ChangeValueMode::ABSOLUTE);
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	if(!selectBulwarkPerkIfActive(defenderSideHero, newHorizonsBulwark::THICK_HIDE_ID))
		GTEST_SKIP() << "Thick Hide is not active in the New Horizons perk rules";
	startBattle();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 5), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(12, 5), 100);
	forceMaximumDamage(shooter);
	defender->defending = true;
	const auto shooterHealthBefore = shooter->getAvailableHealth();
	const auto defenderHealthBefore = defender->getAvailableHealth();
	battle()->activeStack = shooter->unitId();
	const auto action = BattleAction::makeShotAttack(shooter, defender);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(shooter->unitSide()), action));
	ASSERT_EQ(server.attacks.size(), 1u);
	ASSERT_EQ(server.attacks.front().bsa.size(), 1u);
	EXPECT_NE(server.attacks.front().flags & BattleAttack::SHOT, 0);
	const int64_t received = std::min<int64_t>(server.attacks.front().bsa.front().damageAmount, defenderHealthBefore);
	EXPECT_EQ(shooterHealthBefore - shooter->getAvailableHealth(),
		newHorizonsBulwark::reflectedDamage(received, 1250));
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, ThickHideDoesNotReflectSpellLikeRangedCreatureAttacks)
{
	startGame();
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	if(!selectBulwarkPerkIfActive(defenderSideHero, newHorizonsBulwark::THICK_HIDE_ID))
		GTEST_SKIP() << "Thick Hide is not active in the New Horizons perk rules";
	startBattle();
	auto * magog = addStack(BattleSide::ATTACKER, creatureByName("core:magog"), BattleHex(3, 5), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(12, 5), 100);
	ASSERT_TRUE(magog->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
	defender->defending = true;
	const auto magogHealthBefore = magog->getAvailableHealth();
	battle()->activeStack = magog->unitId();
	const auto action = BattleAction::makeShotAttack(magog, defender);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(magog->unitSide()), action));
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_TRUE(server.attacks.front().spellLike());
	EXPECT_EQ(magogHealthBefore, magog->getAvailableHealth());
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, RangedBulwarkReflectionDoesNotLeakToSiegeWeapons)
{
	startGame();
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	if(!selectBulwarkPerkIfActive(defenderSideHero, newHorizonsBulwark::THICK_HIDE_ID))
		GTEST_SKIP() << "Thick Hide is not active in the New Horizons perk rules";
	startBattle();
	auto * ballista = addStack(BattleSide::ATTACKER, creatureByName("core:ballista"), BattleHex(leftHex), 1);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	ASSERT_TRUE(ballista->hasBonusOfType(BonusType::SIEGE_WEAPON));
	defender->defending = true;
	const auto ballistaHealthBefore = ballista->getAvailableHealth();
	battle()->activeStack = ballista->unitId();
	const auto action = BattleAction::makeShotAttack(ballista, defender);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(ballista->unitSide()), action));
	EXPECT_EQ(ballistaHealthBefore, ballista->getAvailableHealth());
}

TEST_F(NewHorizonsBulwarkPerkRuntimeTest, BulwarkDoesNotPreemptOrReflectFromDefendingSiegeWeaponStacks)
{
	startGame();
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	if(!selectBulwarkPerkIfActive(defenderSideHero, newHorizonsBulwark::THICK_HIDE_ID))
		GTEST_SKIP() << "Thick Hide is not active in the New Horizons perk rules";
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 100);
	auto * ballista = addStack(BattleSide::DEFENDER, creatureByName("core:ballista"), BattleHex(81), 1);
	ASSERT_TRUE(ballista->hasBonusOfType(BonusType::SIEGE_WEAPON));
	ballista->defending = true;
	const auto attackerHealthBefore = attacker->getAvailableHealth();
	ASSERT_TRUE(attack(attacker, ballista->getPosition()));
	EXPECT_EQ(server.attacks.size(), 1u);
	EXPECT_EQ(attackerHealthBefore, attacker->getAvailableHealth());
}

INSTANTIATE_TEST_SUITE_P(Ranks, NewHorizonsBulwarkRuntimeTest,
	::testing::Values(
		BulwarkReductionCase{MasteryLevel::NONE, 0},
		BulwarkReductionCase{MasteryLevel::BASIC, 700},
		BulwarkReductionCase{MasteryLevel::ADVANCED, 1050},
		BulwarkReductionCase{MasteryLevel::EXPERT, 1400}));
