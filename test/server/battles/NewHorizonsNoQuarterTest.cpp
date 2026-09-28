/*
 * NewHorizonsNoQuarterTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"
#include "FullGameSnapshotTypes.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsBulwark.h"
#include "../../../lib/battle/NewHorizonsOffense.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

namespace
{
class NewHorizonsNoQuarterTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		auto perkRules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		auto & offensePerks = perkRules["skills"][newHorizonsOffense::SKILL]["perks"].Vector();
		const auto noQuarter = std::find_if(offensePerks.begin(), offensePerks.end(), [](const auto & perk)
		{
			return perk["id"].String() == newHorizonsOffense::NO_QUARTER;
		});
		if(noQuarter == offensePerks.end())
			throw std::runtime_error("No Quarter is missing from the New Horizons perk registry");
		(*noQuarter)["effect"]["status"].String() = "active";
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perkRules);
	}

	void enableNoQuarter(BattleSide side)
	{
		auto * hero = side == BattleSide::ATTACKER ? attackerSideHero : defenderSideHero;
		const int decodedOffense = SecondarySkill::decode(newHorizonsOffense::SKILL);
		ASSERT_GE(decodedOffense, 0);
		hero->setSecSkillLevel(SecondarySkill(decodedOffense), MasteryLevel::EXPERT,
			ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({newHorizonsOffense::SKILL, newHorizonsOffense::NO_QUARTER});
		ASSERT_TRUE(hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::NO_QUARTER));
	}

	void startBattleWithNoQuarter(BattleSide side)
	{
		startGame();
		enableNoQuarter(side);
		startBattle();
	}

	void addTimeStopMarker(CStack * stack)
	{
		const Bonus marker(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP,
			BonusSource::OTHER, 1, BonusSourceID());
		battle()->addOrUpdateUnitBonus(stack, marker, true);
	}

	void setActiveStack(const CStack * stack, BattleUnitTurnReason reason)
	{
		BattleSetActiveStack activation;
		activation.battleID = BattleID(0);
		activation.stack = stack->unitId();
		activation.reason = reason;
		gameHandler->sendAndApply(activation);
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

	bool hasNoQuarterMorale(const CStack * stack) const
	{
		const auto bonuses = stack->getAllBonuses(CSelector([](const Bonus * bonus)
		{
			return newHorizonsOffense::isNoQuarterMoralePenalty(bonus);
		}));
		return bonuses && !bonuses->empty();
	}

	bool hasNoQuarterRetaliationBlock(const CStack * stack) const
	{
		const auto bonuses = stack->getAllBonuses(CSelector([](const Bonus * bonus)
		{
			return newHorizonsOffense::isNoQuarterRetaliationBonus(bonus);
		}));
		return bonuses && !bonuses->empty();
	}

	bool shoot(const CStack * shooter, const CStack * target)
	{
		battle()->activeStack = shooter->unitId();
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(shooter->unitSide()), BattleAction::makeShotAttack(shooter, target));
	}
};
}

TEST_F(NewHorizonsNoQuarterTest, ThresholdIsStrictAndIntegerSafe)
{
	using newHorizonsOffense::belowNoQuarterThreshold;
	EXPECT_FALSE(belowNoQuarterThreshold(0, 100));
	EXPECT_FALSE(belowNoQuarterThreshold(25, 100));
	EXPECT_TRUE(belowNoQuarterThreshold(24, 100));
	EXPECT_TRUE(belowNoQuarterThreshold(1, 5));
	EXPECT_FALSE(belowNoQuarterThreshold(1, 4));
	EXPECT_FALSE(belowNoQuarterThreshold(1, 0));
	EXPECT_TRUE(belowNoQuarterThreshold(std::numeric_limits<int64_t>::max() / 4,
		std::numeric_limits<int64_t>::max()));
}

TEST_F(NewHorizonsNoQuarterTest, ExactQuarterDoesNotTrigger)
{
	startBattleWithNoQuarter(BattleSide::ATTACKER);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(92), 1);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 1000);
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::UNLIMITED_RETALIATIONS, BonusSource::OTHER, 1, BonusSourceID()));
	blockRetaliation(attacker);
	forceMaximumDamage(attacker);
	beginCombat();

	const int64_t quarter = target->getTotalHealth() / 4;
	const int64_t hitDamage = battle()->calculateDmgRange(BattleAttackInfo(attacker, target, 0, false)).damage.max;
	ASSERT_GT(hitDamage, 0);
	ASSERT_LT(quarter + hitDamage, target->getTotalHealth());
	setAvailableHealth(target, quarter + hitDamage);
	server.attacks.clear();
	ASSERT_TRUE(attack(attacker, target->getPosition()));
	ASSERT_EQ(target->getAvailableHealth(), quarter);
	EXPECT_FALSE(hasNoQuarterRetaliationBlock(target));
	EXPECT_FALSE(hasNoQuarterMorale(target));
	EXPECT_TRUE(target->counterAttacks.canUse());
	EXPECT_EQ(target->noQuarterMoraleActivationsRemaining, 0);
}

TEST_F(NewHorizonsNoQuarterTest, BelowQuarterSuppressesImmediateUnlimitedRetaliation)
{
	startBattleWithNoQuarter(BattleSide::ATTACKER);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(92), 1);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 1000);
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::UNLIMITED_RETALIATIONS, BonusSource::OTHER, 1, BonusSourceID()));
	forceMaximumDamage(attacker);
	beginCombat();

	const int64_t quarter = target->getTotalHealth() / 4;
	const int64_t hitDamage = battle()->calculateDmgRange(BattleAttackInfo(attacker, target, 0, false)).damage.max;
	ASSERT_GT(hitDamage, 0);
	setAvailableHealth(target, quarter + hitDamage - 1);

	server.attacks.clear();
	ASSERT_TRUE(attack(attacker, target->getPosition()));
	EXPECT_EQ(target->getAvailableHealth(), quarter - 1);
	EXPECT_TRUE(target->alive());
	EXPECT_TRUE(hasNoQuarterRetaliationBlock(target));
	EXPECT_TRUE(hasNoQuarterMorale(target));
	EXPECT_EQ(target->noQuarterMoraleActivationsRemaining, 1);
	EXPECT_FALSE(target->counterAttacks.canUse());
	ASSERT_EQ(server.attacks.size(), 1u);
	EXPECT_EQ(server.attacks.front().stackAttacking, attacker->unitId());
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const auto & line)
	{
		return line.find("No Quarter affects") != std::string::npos
			&& line.find("all remaining retaliations are lost this round") != std::string::npos
			&& line.find("Morale is reduced by 2") != std::string::npos;
	})) << ::testing::PrintToString(server.battleLogLines);
}

TEST_F(NewHorizonsNoQuarterTest, RetaliationCanTriggerOnTheActiveStackAndMoraleExpiresAfterItsNextActivation)
{
	startBattleWithNoQuarter(BattleSide::DEFENDER);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:ogre"), BattleHex(92), 1);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:gnoll"), BattleHex(93), 1);
	defender->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::STACK_HEALTH, BonusSource::OTHER, 1000, BonusSourceID()));
	forceMaximumDamage(defender);
	beginCombat();

	const int64_t threshold = attacker->getTotalHealth() / 4;
	const int64_t retaliationDamage = battle()->calculateDmgRange(
		BattleAttackInfo(defender, attacker, 0, false)).damage.max;
	ASSERT_GT(retaliationDamage, 0);
	setAvailableHealth(attacker, threshold + retaliationDamage - 1);
	const int64_t primaryDamage = battle()->calculateDmgRange(
		BattleAttackInfo(attacker, defender, 0, false)).damage.max;
	ASSERT_LT(primaryDamage, defender->getAvailableHealth());

	server.attacks.clear();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	EXPECT_LT(attacker->getAvailableHealth(), threshold);
	EXPECT_TRUE(attacker->alive());
	EXPECT_TRUE(hasNoQuarterRetaliationBlock(attacker));
	EXPECT_TRUE(hasNoQuarterMorale(attacker));
	// The counter is initialized to two while this is the active stack, then
	// the accepted attack consumes the remainder of its current activation.
	EXPECT_EQ(attacker->noQuarterMoraleActivationsRemaining, 1);
	ASSERT_EQ(server.attacks.size(), 2u);
	EXPECT_TRUE(server.attacks.back().stackAttacking == defender->unitId());

	setActiveStack(attacker, BattleUnitTurnReason::TURN_QUEUE);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(attacker->unitSide()), BattleAction::makeDefend(attacker)));
	EXPECT_EQ(attacker->noQuarterMoraleActivationsRemaining, 0);
	EXPECT_FALSE(hasNoQuarterMorale(attacker));
	EXPECT_TRUE(hasNoQuarterRetaliationBlock(attacker));
}

TEST_F(NewHorizonsNoQuarterTest, RefreshIsIdempotentAndRoundResetPreservesUnrelatedBlockers)
{
	startBattleWithNoQuarter(BattleSide::ATTACKER);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(92), 1);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 1000);
	forceMaximumDamage(attacker);
	attacker->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::NO_RETALIATION, BonusSource::OTHER, 1, BonusSourceID()));
	beginCombat();

	const int64_t quarter = target->getTotalHealth() / 4;
	const int64_t hitDamage = battle()->calculateDmgRange(BattleAttackInfo(attacker, target, 0, false)).damage.max;
	ASSERT_GT(hitDamage, 0);
	setAvailableHealth(target, quarter + 2 * hitDamage - 1);
	server.attacks.clear();
	ASSERT_TRUE(attack(attacker, target->getPosition()));
	EXPECT_TRUE(hasNoQuarterRetaliationBlock(target));
	EXPECT_TRUE(hasNoQuarterMorale(target));
	EXPECT_EQ(target->noQuarterMoraleActivationsRemaining, 1);
	const auto noQuarterBlockers = target->getAllBonuses(CSelector([](const Bonus * bonus)
	{
		return newHorizonsOffense::isNoQuarterRetaliationBonus(bonus);
	}));
	const auto noQuarterMoralePenalties = target->getAllBonuses(CSelector([](const Bonus * bonus)
	{
		return newHorizonsOffense::isNoQuarterMoralePenalty(bonus);
	}));
	ASSERT_NE(noQuarterBlockers, nullptr);
	ASSERT_NE(noQuarterMoralePenalties, nullptr);
	EXPECT_EQ(noQuarterBlockers->size(), 1u);
	EXPECT_EQ(noQuarterMoralePenalties->size(), 1u);
	EXPECT_TRUE(battle()->hasNoQuarterState());

	setActiveStack(target, BattleUnitTurnReason::TURN_QUEUE);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(target->unitSide()), BattleAction::makeDefend(target)));
	EXPECT_FALSE(hasNoQuarterMorale(target));
	EXPECT_TRUE(hasNoQuarterRetaliationBlock(target));

	endRound();
	EXPECT_FALSE(hasNoQuarterRetaliationBlock(target));
	EXPECT_TRUE(target->hasBonus(Selector::type()(BonusType::NO_RETALIATION)));
	EXPECT_FALSE(battle()->hasNoQuarterState()); // The unrelated permanent blocker remains.
}

TEST_F(NewHorizonsNoQuarterTest, RangedAndSpellLikeHitsDoNotApplyTheMeleePerk)
{
	startBattleWithNoQuarter(BattleSide::ATTACKER);
	auto * archer = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(70), 1);
	auto * magog = addStack(BattleSide::ATTACKER, creatureByName("core:magog"), BattleHex(71), 1);
	auto * archerTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(110), 1000);
	auto * magogTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(111), 1000);
	forceMaximumDamage(archer);
	forceMaximumDamage(magog);
	beginCombat();
	ASSERT_TRUE(archer->isShooter());
	ASSERT_TRUE(magog->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));

	auto makeLowHealthForShot = [&](CStack * target)
	{
		setAvailableHealth(target, target->getTotalHealth() / 4 - 1);
	};

	makeLowHealthForShot(archerTarget);
	const int64_t archerHealth = archerTarget->getAvailableHealth();
	ASSERT_TRUE(shoot(archer, archerTarget));
	EXPECT_TRUE(archerTarget->alive());
	EXPECT_LT(archerTarget->getAvailableHealth(), archerHealth);
	EXPECT_TRUE(newHorizonsOffense::belowNoQuarterThreshold(
		archerTarget->getAvailableHealth(), archerTarget->getTotalHealth()));
	EXPECT_FALSE(hasNoQuarterRetaliationBlock(archerTarget));
	EXPECT_FALSE(hasNoQuarterMorale(archerTarget));

	makeLowHealthForShot(magogTarget);
	const int64_t magogHealth = magogTarget->getAvailableHealth();
	ASSERT_TRUE(shoot(magog, magogTarget));
	EXPECT_TRUE(magogTarget->alive());
	EXPECT_LT(magogTarget->getAvailableHealth(), magogHealth);
	EXPECT_TRUE(newHorizonsOffense::belowNoQuarterThreshold(
		magogTarget->getAvailableHealth(), magogTarget->getTotalHealth()));
	EXPECT_FALSE(hasNoQuarterRetaliationBlock(magogTarget));
	EXPECT_FALSE(hasNoQuarterMorale(magogTarget));

	auto * closeMagog = addStack(BattleSide::ATTACKER, creatureByName("core:magog"), BattleHex(90), 1);
	auto * meleeTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(91), 1000);
	ASSERT_TRUE(closeMagog->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
	forceMaximumDamage(closeMagog);
	blockRetaliation(closeMagog);
	const int64_t meleeDamage = battle()->calculateDmgRange(BattleAttackInfo(
		closeMagog, meleeTarget, 0, false)).damage.max;
	ASSERT_GT(meleeDamage, 0);
	setAvailableHealth(meleeTarget, meleeTarget->getTotalHealth() / 4 + meleeDamage - 1);
	ASSERT_TRUE(attack(closeMagog, meleeTarget->getPosition()));
	EXPECT_TRUE(meleeTarget->alive());
	EXPECT_TRUE(hasNoQuarterRetaliationBlock(meleeTarget));
	EXPECT_TRUE(hasNoQuarterMorale(meleeTarget));
}

TEST_F(NewHorizonsNoQuarterTest, PhysicalMeleeCollateralCanTriggerNoQuarter)
{
	startBattleWithNoQuarter(BattleSide::ATTACKER);
	auto * hydra = addStack(BattleSide::ATTACKER, creatureByName("core:hydra"), BattleHex(leftHex), 10);
	auto * primary = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	auto * collateral = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(leftHex - 17), 100);
	forceMaximumDamage(hydra);
	blockRetaliation(hydra);
	BattleAttackInfo collateralAttack(hydra, collateral, 0, false);
	collateralAttack.secondaryAttack = true;
	const int64_t hitDamage = battle()->calculateDmgRange(collateralAttack).damage.max;
	ASSERT_GT(hitDamage, 0);
	setAvailableHealth(collateral, collateral->getTotalHealth() / 4 + hitDamage - 1);
	beginCombat();

	server.attacks.clear();
	ASSERT_TRUE(attack(hydra, primary->getPosition()));
	ASSERT_FALSE(server.attacks.empty());
	const auto collateralHit = std::ranges::find(server.attacks.front().bsa,
		collateral->unitId(), &BattleStackAttacked::stackAttacked);
	ASSERT_NE(collateralHit, server.attacks.front().bsa.end());
	EXPECT_TRUE(collateralHit->flags & BattleStackAttacked::SECONDARY);
	EXPECT_TRUE(collateral->alive());
	EXPECT_TRUE(hasNoQuarterRetaliationBlock(collateral));
	EXPECT_TRUE(hasNoQuarterMorale(collateral));
}

TEST_F(NewHorizonsNoQuarterTest, TimeStoppedCollateralDoesNotReceiveNoQuarterAndRoundCleanupStillRuns)
{
	startBattleWithNoQuarter(BattleSide::ATTACKER);
	auto * hydra = addStack(BattleSide::ATTACKER, creatureByName("core:hydra"), BattleHex(leftHex), 10);
	auto * primary = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	auto * stopped = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(leftHex - 17), 100);
	forceMaximumDamage(hydra);
	blockRetaliation(hydra);
	BattleAttackInfo collateralAttack(hydra, stopped, 0, false);
	collateralAttack.secondaryAttack = true;
	const int64_t hitDamage = battle()->calculateDmgRange(collateralAttack).damage.max;
	ASSERT_GT(hitDamage, 0);
	setAvailableHealth(stopped, stopped->getTotalHealth() / 4 + hitDamage - 1);
	addTimeStopMarker(stopped);
	ASSERT_TRUE(stopped->isTimeStopped());
	beginCombat();

	server.attacks.clear();
	ASSERT_TRUE(attack(hydra, primary->getPosition()));
	ASSERT_FALSE(server.attacks.empty());
	const auto stoppedHit = std::ranges::find(server.attacks.front().bsa,
		stopped->unitId(), &BattleStackAttacked::stackAttacked);
	ASSERT_NE(stoppedHit, server.attacks.front().bsa.end());
	EXPECT_TRUE(stoppedHit->flags & BattleStackAttacked::SECONDARY);
	EXPECT_TRUE(stopped->alive());
	EXPECT_FALSE(hasNoQuarterRetaliationBlock(stopped));
	EXPECT_FALSE(hasNoQuarterMorale(stopped));
	EXPECT_EQ(stopped->noQuarterMoraleActivationsRemaining, 0);

	SetStackEffect staleEffect;
	staleEffect.battleID = BattleID(0);
	staleEffect.toAdd.emplace_back(stopped->unitId(), std::vector<Bonus>{
		newHorizonsOffense::noQuarterRetaliationBonus()});
	gameHandler->sendAndApply(staleEffect);
	EXPECT_FALSE(hasNoQuarterRetaliationBlock(stopped));

	// Simulate stale tagged battle state from an old save. Unlike N_TURNS, the
	// No Quarter round blocker is always cleared at the round seam, even in stasis.
	stopped->addNewBonus(std::make_shared<Bonus>(newHorizonsOffense::noQuarterRetaliationBonus()));
	ASSERT_TRUE(hasNoQuarterRetaliationBlock(stopped));
	battle()->nextRound();
	EXPECT_FALSE(hasNoQuarterRetaliationBlock(stopped));
	EXPECT_TRUE(stopped->isTimeStopped());
}

TEST_F(NewHorizonsNoQuarterTest, PhantomIntegrityDefinesTheMaximumHealthThreshold)
{
	startBattleWithNoQuarter(BattleSide::ATTACKER);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(92), 1);
	auto * phantom = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 1);
	phantom->summoned = true;
	phantom->initializePhantomProfile(400, 2);
	forceMaximumDamage(attacker);
	blockRetaliation(attacker);
	beginCombat();

	const int64_t phantomMaximum = battle::getMaximumHealth(*phantom);
	ASSERT_EQ(phantomMaximum, 400);
	const int64_t threshold = phantomMaximum / 4;
	const int64_t hitDamage = battle()->calculateDmgRange(BattleAttackInfo(attacker, phantom, 0, false)).damage.max;
	ASSERT_GT(hitDamage, 0);
	ASSERT_LT(threshold + hitDamage - 1, phantom->getPhantomInitialIntegrity());
	setAvailableHealth(phantom, threshold + hitDamage - 1);
	ASSERT_TRUE(attack(attacker, phantom->getPosition()));
	ASSERT_TRUE(phantom->alive());
	EXPECT_LT(phantom->getAvailableHealth(), threshold);
	EXPECT_FALSE(newHorizonsOffense::belowNoQuarterThreshold(
		phantom->getAvailableHealth(), phantom->getTotalHealth()));
	EXPECT_TRUE(hasNoQuarterRetaliationBlock(phantom));
	EXPECT_TRUE(hasNoQuarterMorale(phantom));
}

TEST_F(NewHorizonsNoQuarterTest, CleaveHitCanTriggerNoQuarter)
{
	startGame();
	enableNoQuarter(BattleSide::ATTACKER);
	attackerSideHero->applyPerkSelection({newHorizonsOffense::SKILL, newHorizonsOffense::CLEAVE});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::CLEAVE));
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(92), 100);
	auto * destroyed = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 1);
	auto * cleaveTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(76), 1000);
	forceMaximumDamage(attacker);
	BattleAttackInfo cleaveAttack(attacker, cleaveTarget, 0, false);
	cleaveAttack.cleaveDamagePercent = newHorizonsOffense::CLEAVE_DAMAGE_PERCENT;
	const int64_t cleaveDamage = battle()->calculateDmgRange(cleaveAttack).damage.max;
	ASSERT_GT(cleaveDamage, 0);
	setAvailableHealth(cleaveTarget, cleaveTarget->getTotalHealth() / 4 + cleaveDamage - 1);
	beginCombat();

	server.attacks.clear();
	ASSERT_TRUE(attack(attacker, destroyed->getPosition()));
	ASSERT_GE(server.attacks.size(), 2u);
	EXPECT_EQ(server.attacks.back().bsa.front().stackAttacked, cleaveTarget->unitId());
	EXPECT_TRUE(hasNoQuarterRetaliationBlock(cleaveTarget));
	EXPECT_TRUE(hasNoQuarterMorale(cleaveTarget));
}

TEST_F(NewHorizonsNoQuarterTest, BulwarkPreemptivePhysicalHitCanTriggerNoQuarter)
{
	startGame();
	enableNoQuarter(BattleSide::DEFENDER);
	const int decodedBulwark = SecondarySkill::decode(std::string(newHorizonsBulwark::SKILL_ID));
	ASSERT_GE(decodedBulwark, 0);
	defenderSideHero->setSecSkillLevel(SecondarySkill(decodedBulwark), MasteryLevel::BASIC,
		ChangeValueMode::ABSOLUTE);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 100);
	defender->defending = true;
	forceMaximumDamage(defender);
	BattleAttackInfo preemptive(defender, attacker, 0, false);
	preemptive.preemptiveDamagePercent = newHorizonsBulwark::preemptivePercent(1);
	const int64_t preemptiveDamage = battle()->calculateDmgRange(preemptive).damage.max;
	ASSERT_GT(preemptiveDamage, 0);
	setAvailableHealth(attacker, attacker->getTotalHealth() / 4 + preemptiveDamage - 1);
	beginCombat();

	server.attacks.clear();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_EQ(server.attacks.front().stackAttacking, defender->unitId());
	EXPECT_TRUE(hasNoQuarterRetaliationBlock(attacker));
	EXPECT_TRUE(hasNoQuarterMorale(attacker));
}

TEST_F(NewHorizonsNoQuarterTest, SerializedStateAndEffectPacketsRejectOlderWriters)
{
	startBattleWithNoQuarter(BattleSide::ATTACKER);
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(92), 1);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 1000);
	forceMaximumDamage(attacker);
	beginCombat();
	const int64_t quarter = target->getTotalHealth() / 4;
	const int64_t damage = battle()->calculateDmgRange(BattleAttackInfo(attacker, target, 0, false)).damage.max;
	setAvailableHealth(target, quarter + damage - 1);
	ASSERT_TRUE(attack(attacker, target->getPosition()));
	ASSERT_TRUE(battle()->hasNoQuarterState());

	const auto restoredBattle = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_TRUE(restoredBattle->hasNoQuarterState());
	const auto * restoredStack = restoredBattle->getStack(target->unitId(), false);
	ASSERT_NE(restoredStack, nullptr);
	EXPECT_EQ(restoredStack->noQuarterMoraleActivationsRemaining, 1);

	BattleStart outgoing;
	outgoing.battleID = BattleID(0);
	outgoing.info = CMemorySerializer::deepCopy(*battle(), gameState().get());
	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	current.oser & outgoing;
	current.iser.cb = gameState().get();
	BattleStart incoming;
	current.iser & incoming;
	ASSERT_NE(incoming.info, nullptr);
	EXPECT_TRUE(incoming.info->hasNoQuarterState());

	BattleUnitsChanged statePacket;
	statePacket.battleID = BattleID(0);
	UnitChanges stateChange(target->unitId(), UnitChanges::EOperation::UPDATE);
	stateChange.data = target->acquireState()->save();
	statePacket.changedStacks.push_back(std::move(stateChange));
	ASSERT_TRUE(statePacket.changedStacks.front().hasNoQuarterMoraleState());
	CMemorySerializer currentStatePacket;
	currentStatePacket.oser.version = ESerializationVersion::CURRENT;
	currentStatePacket.iser.version = ESerializationVersion::CURRENT;
	currentStatePacket.oser & statePacket;
	currentStatePacket.iser.cb = gameState().get();
	BattleUnitsChanged decodedStatePacket;
	currentStatePacket.iser & decodedStatePacket;
	ASSERT_EQ(decodedStatePacket.changedStacks.size(), 1u);
	EXPECT_TRUE(decodedStatePacket.changedStacks.front().hasNoQuarterMoraleState());
	CMemorySerializer oldStatePacketWriter;
	oldStatePacketWriter.oser.version = ESerializationVersion::NEW_HORIZONS_RELENTLESS_ASSAULT;
	EXPECT_THROW(oldStatePacketWriter.oser & statePacket, std::runtime_error);
	EXPECT_TRUE(oldStatePacketWriter.extractBuffer().empty());

	BattleAttack attackPacket = server.attacks.front();
	UnitChanges attackState(target->unitId(), UnitChanges::EOperation::UPDATE);
	attackState.data = target->acquireState()->save();
	attackPacket.attackerChanges.changedStacks.push_back(std::move(attackState));
	CMemorySerializer currentAttackPacket;
	currentAttackPacket.oser.version = ESerializationVersion::CURRENT;
	currentAttackPacket.iser.version = ESerializationVersion::CURRENT;
	currentAttackPacket.oser & attackPacket;
	currentAttackPacket.iser.cb = gameState().get();
	BattleAttack decodedAttackPacket;
	currentAttackPacket.iser & decodedAttackPacket;
	EXPECT_TRUE(std::ranges::any_of(decodedAttackPacket.attackerChanges.changedStacks,
		[](const UnitChanges & change) { return change.hasNoQuarterMoraleState(); }));
	CMemorySerializer oldAttackWriter;
	oldAttackWriter.oser.version = ESerializationVersion::NEW_HORIZONS_RELENTLESS_ASSAULT;
	EXPECT_THROW(oldAttackWriter.oser & attackPacket, std::runtime_error);
	EXPECT_TRUE(oldAttackWriter.extractBuffer().empty());

	CMemorySerializer oldBattleWriter;
	oldBattleWriter.oser.version = ESerializationVersion::NEW_HORIZONS_RELENTLESS_ASSAULT;
	EXPECT_THROW(oldBattleWriter.oser & *battle(), std::runtime_error);
	EXPECT_TRUE(oldBattleWriter.extractBuffer().empty());
	CMemorySerializer oldStartWriter;
	oldStartWriter.oser.version = ESerializationVersion::NEW_HORIZONS_RELENTLESS_ASSAULT;
	EXPECT_THROW(oldStartWriter.oser & outgoing, std::runtime_error);
	EXPECT_TRUE(oldStartWriter.extractBuffer().empty());

	SetStackEffect effect;
	effect.battleID = BattleID(0);
	effect.toAdd.emplace_back(target->unitId(), std::vector<Bonus>{
		newHorizonsOffense::noQuarterRetaliationBonus()});
	CMemorySerializer oldEffectWriter;
	oldEffectWriter.oser.version = ESerializationVersion::NEW_HORIZONS_RELENTLESS_ASSAULT;
	EXPECT_THROW(oldEffectWriter.oser & effect, std::runtime_error);
	EXPECT_TRUE(oldEffectWriter.extractBuffer().empty());
}
