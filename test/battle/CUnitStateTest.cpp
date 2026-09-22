/*
 * CUnitStateTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "mock/mock_BonusBearer.h"
#include "mock/mock_UnitInfo.h"
#include "mock/mock_UnitEnvironment.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/json/JsonNode.h"

namespace test
{
using namespace ::testing;

static const int32_t DEFAULT_HP = 123;
static const int32_t DEFAULT_AMOUNT = 100;
static const int32_t DEFAULT_SPEED = 10;
static const BattleHex DEFAULT_POSITION = BattleHex(5, 5);
static const int DEFAULT_ATTACK = 58;
static const int DEFAULT_DEFENCE = 63;

class UnitStateTest : public Test
{
public:
	UnitInfoMock infoMock;
	UnitEnvironmentMock envMock;
	BonusBearerMock bonusMock;

	const CCreature * pikeman;

	battle::CUnitStateDetached subject;

	bool hasAmmoCart;

	UnitStateTest()
		:infoMock(),
		envMock(),
		bonusMock(),
		subject(&infoMock, &bonusMock),
		hasAmmoCart(false)
	{
		pikeman = CreatureID(0).toCreature();
	}

	void setDefaultExpectations()
	{
		bonusMock.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::CREATURE_ABILITY, DEFAULT_SPEED, BonusSourceID()));

		bonusMock.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::PRIMARY_SKILL, BonusSource::CREATURE_ABILITY, DEFAULT_ATTACK, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK)));
		bonusMock.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::PRIMARY_SKILL, BonusSource::CREATURE_ABILITY, DEFAULT_DEFENCE, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE)));

		bonusMock.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, DEFAULT_HP, BonusSourceID()));

		EXPECT_CALL(infoMock, unitBaseAmount()).WillRepeatedly(Return(DEFAULT_AMOUNT));
		EXPECT_CALL(infoMock, unitType()).WillRepeatedly(Return(pikeman));

		EXPECT_CALL(envMock, unitHasAmmoCart(_)).WillRepeatedly(Return(hasAmmoCart));
	}

	void makeShooter(int32_t ammo)
	{
		bonusMock.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SHOOTER, BonusSource::CREATURE_ABILITY, 1, BonusSourceID()));
		bonusMock.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SHOTS, BonusSource::CREATURE_ABILITY, ammo, BonusSourceID()));
	}

	void initUnit()
	{
		subject.localInit(&envMock);
		subject.position = DEFAULT_POSITION;
	}
};

TEST_F(UnitStateTest, initialRegular)
{
	setDefaultExpectations();
	initUnit();

	EXPECT_TRUE(subject.alive());
	EXPECT_TRUE(subject.ableToRetaliate());
	EXPECT_FALSE(subject.isGhost());
	EXPECT_FALSE(subject.isDead());
	EXPECT_FALSE(subject.isTurret());
	EXPECT_TRUE(subject.isValidTarget(true));
	EXPECT_TRUE(subject.isValidTarget(false));

	EXPECT_FALSE(subject.isClone());
	EXPECT_FALSE(subject.hasClone());

	EXPECT_FALSE(subject.canCast());
	EXPECT_FALSE(subject.isCaster());
	EXPECT_FALSE(subject.canShoot());
	EXPECT_FALSE(subject.isShooter());

	EXPECT_EQ(subject.getCount(), DEFAULT_AMOUNT);
	EXPECT_EQ(subject.getFirstHPleft(), DEFAULT_HP);
	EXPECT_EQ(subject.getKilled(), 0);
	EXPECT_EQ(subject.getUnusableRemains(), 0);
	EXPECT_EQ(subject.getAvailableHealth(), DEFAULT_HP * DEFAULT_AMOUNT);
	EXPECT_EQ(subject.getTotalHealth(), subject.getAvailableHealth());

	EXPECT_EQ(subject.getPosition(), DEFAULT_POSITION);

	EXPECT_EQ(subject.getInitiative(), DEFAULT_SPEED);
	EXPECT_EQ(subject.getInitiative(123456), DEFAULT_SPEED);

	EXPECT_TRUE(subject.canMove());
	EXPECT_TRUE(subject.canMove(123456));
	EXPECT_FALSE(subject.defended());
	EXPECT_FALSE(subject.defended(123456));
	EXPECT_FALSE(subject.moved());
	EXPECT_FALSE(subject.moved(123456));
	EXPECT_TRUE(subject.willMove());
	EXPECT_TRUE(subject.willMove(123456));
	EXPECT_FALSE(subject.waited());
	EXPECT_FALSE(subject.waited(123456));

	EXPECT_EQ(subject.getTotalAttacks(true), 1);
	EXPECT_EQ(subject.getTotalAttacks(false), 1);
}

TEST_F(UnitStateTest, explicitInitiativeIsIndependentFromMovementSpeed)
{
	setDefaultExpectations();
	bonusMock.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_INITIATIVE_BASE, BonusSource::CREATURE_ABILITY, 17, BonusSourceID()));
	initUnit();

	EXPECT_EQ(subject.getMovementRange(), DEFAULT_SPEED);
	EXPECT_EQ(subject.getInitiative(), 17);
	EXPECT_EQ(subject.getInitiative(123456), 17);
}

TEST_F(UnitStateTest, sameSpeedDifferentInitiativeChangesBattleQueueOnly)
{
	UnitInfoMock slowInfo;
	UnitInfoMock fastInfo;
	BonusBearerMock slowBonuses;
	BonusBearerMock fastBonuses;

	for(auto * bonuses : {&slowBonuses, &fastBonuses})
	{
		bonuses->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::CREATURE_ABILITY, DEFAULT_SPEED, BonusSourceID()));
	}
	slowBonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_INITIATIVE_BASE, BonusSource::CREATURE_ABILITY, 8, BonusSourceID()));
	fastBonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_INITIATIVE_BASE, BonusSource::CREATURE_ABILITY, 12, BonusSourceID()));

	battle::CUnitStateDetached slow(&slowInfo, &slowBonuses);
	battle::CUnitStateDetached fast(&fastInfo, &fastBonuses);

	EXPECT_EQ(slow.getMovementRange(), fast.getMovementRange());
	EXPECT_LT(slow.getInitiative(), fast.getInitiative());
	EXPECT_TRUE(CMP_stack{}(&fast, &slow));
	EXPECT_FALSE(CMP_stack{}(&slow, &fast));
}

TEST_F(UnitStateTest, missingInitiativeFallsBackToSpeed)
{
	setDefaultExpectations();
	initUnit();

	EXPECT_EQ(subject.getMovementRange(), DEFAULT_SPEED);
	EXPECT_EQ(subject.getInitiative(), DEFAULT_SPEED);
	EXPECT_EQ(subject.getInitiative(123456), DEFAULT_SPEED);
}

TEST_F(UnitStateTest, explicitInitiativeIgnoresTemporarySpeedModifier)
{
	setDefaultExpectations();
	bonusMock.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_INITIATIVE_BASE, BonusSource::CREATURE_ABILITY, DEFAULT_SPEED, BonusSourceID()));
	auto frost = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, -2, BonusSourceID());
	frost->turnsRemain = 2;
	bonusMock.addNewBonus(frost);
	initUnit();

	EXPECT_EQ(subject.getMovementRange(), DEFAULT_SPEED - 2);
	EXPECT_EQ(subject.getInitiative(), DEFAULT_SPEED);
	EXPECT_EQ(subject.getInitiative(1), DEFAULT_SPEED);
}

TEST_F(UnitStateTest, legacyInitiativeTracksTemporarySpeedModifier)
{
	setDefaultExpectations();
	auto frost = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, -2, BonusSourceID());
	frost->turnsRemain = 2;
	bonusMock.addNewBonus(frost);
	initUnit();

	EXPECT_EQ(subject.getMovementRange(), DEFAULT_SPEED - 2);
	EXPECT_EQ(subject.getInitiative(), DEFAULT_SPEED - 2);
	EXPECT_EQ(subject.getInitiative(1), DEFAULT_SPEED - 2);
}

TEST_F(UnitStateTest, movementRangeBonusDoesNotAffectLegacyInitiative)
{
	setDefaultExpectations();
	bonusMock.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_MOVEMENT_RANGE, BonusSource::SPELL_EFFECT, 3, BonusSourceID()));
	initUnit();

	EXPECT_EQ(subject.getMovementRange(), DEFAULT_SPEED + 3);
	EXPECT_EQ(subject.getInitiative(), DEFAULT_SPEED);
	EXPECT_EQ(subject.getInitiative(1), DEFAULT_SPEED);
}

TEST_F(UnitStateTest, movementRangeBonusCannotUnderflow)
{
	setDefaultExpectations();
	bonusMock.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_MOVEMENT_RANGE, BonusSource::SPELL_EFFECT, -DEFAULT_SPEED - 1, BonusSourceID()));
	initUnit();

	EXPECT_EQ(subject.getMovementRange(), 0u);
	EXPECT_EQ(subject.getInitiative(), DEFAULT_SPEED);
}

TEST_F(UnitStateTest, canShoot)
{
	setDefaultExpectations();
	makeShooter(1);
	initUnit();

	EXPECT_FALSE(subject.canCast());
	EXPECT_FALSE(subject.isCaster());
	EXPECT_TRUE(subject.canShoot());
	EXPECT_TRUE(subject.isShooter());

	subject.afterAttack(true, false);

	EXPECT_FALSE(subject.canShoot());
	EXPECT_TRUE(subject.isShooter());
}

TEST_F(UnitStateTest, canShootWithAmmoCart)
{
	hasAmmoCart = true;
	setDefaultExpectations();
	makeShooter(1);
	initUnit();

	EXPECT_FALSE(subject.canCast());
	EXPECT_FALSE(subject.isCaster());
	EXPECT_TRUE(subject.canShoot());
	EXPECT_TRUE(subject.isShooter());

	subject.afterAttack(true, false);

	EXPECT_TRUE(subject.canShoot());
	EXPECT_TRUE(subject.isShooter());
}

TEST_F(UnitStateTest, battlecraftWaitBonusIsCopiedSerializedConsumedAndResetPerRound)
{
	setDefaultExpectations();
	initUnit();

	subject.afterWait();
	EXPECT_TRUE(subject.waiting);
	EXPECT_TRUE(subject.waitedThisTurn);
	EXPECT_FALSE(subject.battlecraftWaitBonusUsed);

	const auto saved = subject.save();
	battle::CUnitStateDetached restored(&infoMock, &bonusMock);
	restored.localInit(&envMock);
	restored.load(saved);
	EXPECT_TRUE(restored.waitedThisTurn);
	EXPECT_FALSE(restored.battlecraftWaitBonusUsed);

	// Spell-like effects do not consume a physical-only Wait bonus.
	restored.afterAttack(false, false, false);
	EXPECT_FALSE(restored.battlecraftWaitBonusUsed);

	// The first physical attack or retaliation spends it exactly once.
	restored.afterAttack(false, false, true);
	EXPECT_TRUE(restored.battlecraftWaitBonusUsed);
	restored.afterAttack(false, true, true);
	EXPECT_TRUE(restored.battlecraftWaitBonusUsed);
	restored.afterWait();
	EXPECT_TRUE(restored.battlecraftWaitBonusUsed);

	// A detached/hypothetical copy carries the spent state, and the round
	// boundary clears both the wait provenance and its one-shot consumption.
	battle::CUnitStateDetached copy(&infoMock, &bonusMock);
	copy.localInit(&envMock);
	copy = restored;
	EXPECT_TRUE(copy.battlecraftWaitBonusUsed);
	copy.afterNewRound();
	EXPECT_FALSE(copy.waitedThisTurn);
	EXPECT_FALSE(copy.battlecraftWaitBonusUsed);
}

TEST_F(UnitStateTest, getAttack)
{
	setDefaultExpectations();

	EXPECT_EQ(subject.getAttack(false), DEFAULT_ATTACK);
	EXPECT_EQ(subject.getAttack(true), DEFAULT_ATTACK);
}

TEST_F(UnitStateTest, getDefense)
{
	setDefaultExpectations();

	EXPECT_EQ(subject.getDefense(false), DEFAULT_DEFENCE);
	EXPECT_EQ(subject.getDefense(true), DEFAULT_DEFENCE);
}

// Frenzy no longer moves these two - what it converts depends on the unit being attacked, so it is
// resolved by the damage calculator and covered by DamageCalculatorTest.

TEST_F(UnitStateTest, additionalAttack)
{
	setDefaultExpectations();

	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::ADDITIONAL_ATTACK, BonusSource::SPELL_EFFECT, 41, BonusSourceID());

		bonusMock.addNewBonus(bonus);
	}

	EXPECT_EQ(subject.getTotalAttacks(false), 42);
	EXPECT_EQ(subject.getTotalAttacks(true), 42);
}

TEST_F(UnitStateTest, additionalMeleeAttack)
{
	setDefaultExpectations();

	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::ADDITIONAL_ATTACK, BonusSource::SPELL_EFFECT, 41, BonusSourceID());
		bonus->effectRange = BonusLimitEffect::ONLY_MELEE_FIGHT;

		bonusMock.addNewBonus(bonus);
	}

	EXPECT_EQ(subject.getTotalAttacks(false), 42);
	EXPECT_EQ(subject.getTotalAttacks(true), 1);
}

TEST_F(UnitStateTest, hypnotized)
{
	setDefaultExpectations();

	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::HYPNOTIZED, BonusSource::SPELL_EFFECT, 41, BonusSourceID());

		bonusMock.addNewBonus(bonus);
	}

	EXPECT_TRUE(subject.isHypnotized());
}

TEST_F(UnitStateTest, additionalRangedAttack)
{
	setDefaultExpectations();

	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::ADDITIONAL_ATTACK, BonusSource::SPELL_EFFECT, 41, BonusSourceID());
		bonus->effectRange = BonusLimitEffect::ONLY_DISTANCE_FIGHT;

		bonusMock.addNewBonus(bonus);
	}

	EXPECT_EQ(subject.getTotalAttacks(false), 1);
	EXPECT_EQ(subject.getTotalAttacks(true), 42);
}

TEST_F(UnitStateTest, getMinDamage)
{
	setDefaultExpectations();

	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::CREATURE_DAMAGE, BonusSource::SPELL_EFFECT, 30, BonusSourceID(), BonusCustomSubtype::creatureDamageBoth);
		bonusMock.addNewBonus(bonus);

		bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::CREATURE_DAMAGE, BonusSource::SPELL_EFFECT, -20, BonusSourceID(), BonusCustomSubtype::creatureDamageMin);
		bonus->effectRange = BonusLimitEffect::ONLY_DISTANCE_FIGHT;
		bonusMock.addNewBonus(bonus);

		bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::CREATURE_DAMAGE, BonusSource::SPELL_EFFECT, -10, BonusSourceID(), BonusCustomSubtype::creatureDamageMin);
		bonus->effectRange = BonusLimitEffect::ONLY_MELEE_FIGHT;
		bonusMock.addNewBonus(bonus);

	}

	EXPECT_EQ(subject.getMinDamage(false), 20);
	EXPECT_EQ(subject.getMinDamage(true), 10);
}

TEST_F(UnitStateTest, getMaxDamage)
{
	setDefaultExpectations();

	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::CREATURE_DAMAGE, BonusSource::SPELL_EFFECT, 30, BonusSourceID(), BonusCustomSubtype::creatureDamageBoth);
		bonusMock.addNewBonus(bonus);

		bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::CREATURE_DAMAGE, BonusSource::SPELL_EFFECT, -20, BonusSourceID(), BonusCustomSubtype::creatureDamageMax);
		bonus->effectRange = BonusLimitEffect::ONLY_DISTANCE_FIGHT;
		bonusMock.addNewBonus(bonus);

		bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::CREATURE_DAMAGE, BonusSource::SPELL_EFFECT, -10, BonusSourceID(), BonusCustomSubtype::creatureDamageMax);
		bonus->effectRange = BonusLimitEffect::ONLY_MELEE_FIGHT;
		bonusMock.addNewBonus(bonus);
	}

	EXPECT_EQ(subject.getMaxDamage(false), 20);
	EXPECT_EQ(subject.getMaxDamage(true), 10);
}

TEST_F(UnitStateTest, destroyRemainsIsSerializedAndLegacyDamageDoesNotMarkIt)
{
	setDefaultExpectations();
	initUnit();

	int64_t damage = DEFAULT_HP * 2;
	subject.damage(damage);
	EXPECT_EQ(subject.getUnusableRemains(), 0);

	damage = DEFAULT_HP * 2;
	subject.damage(damage, true);
	EXPECT_EQ(subject.getKilled(), 4);
	EXPECT_EQ(subject.getUnusableRemains(), 2);

	int64_t heal = DEFAULT_HP * 2;
	EXPECT_EQ(subject.heal(heal, EHealLevel::RESURRECT, EHealPower::PERMANENT).resurrectedCount, 2);
	EXPECT_EQ(heal, DEFAULT_HP * 2);
	EXPECT_EQ(subject.getCount(), DEFAULT_AMOUNT - 2);
	EXPECT_EQ(subject.getUnusableRemains(), 2);

	const auto saved = subject.save();
	ASSERT_EQ(saved["state"]["health"]["unusableRemains"].Integer(), 2);

	battle::CUnitStateDetached restored(&infoMock, &bonusMock);
	restored.localInit(&envMock);
	restored.load(saved);
	EXPECT_EQ(restored.getUnusableRemains(), 2);

	// Saves produced before the ledger existed omit the optional field and
	// therefore retain ordinary resurrection semantics.
	auto legacySaved = saved;
	legacySaved["state"]["health"].Struct().erase("unusableRemains");
	restored.load(legacySaved);
	EXPECT_EQ(restored.getUnusableRemains(), 0);
}

TEST_F(UnitStateTest, removedGhostRetainsDestroyedRemainsForBattleResult)
{
	setDefaultExpectations();
	initUnit();

	int64_t damage = DEFAULT_HP * DEFAULT_AMOUNT;
	subject.damage(damage, true);
	EXPECT_EQ(subject.getUnusableRemains(), DEFAULT_AMOUNT);

	subject.onRemoved();
	EXPECT_TRUE(subject.isGhost());
	EXPECT_EQ(subject.getCount(), 0);
	EXPECT_EQ(subject.getUnusableRemains(), DEFAULT_AMOUNT);
}

}
