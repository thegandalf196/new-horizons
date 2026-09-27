/*
 * NewHorizonsBulwarkRuntimeTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/NewHorizonsBulwark.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

namespace
{
bool selectBulwarkPerkIfActive(CGHeroInstance * hero, std::string_view perkId)
{
	const std::string perkIdentifier(perkId);
	const auto & perks = hero->getPerkState().rules["skills"][std::string(newHorizonsBulwark::SKILL_ID)]["perks"].Vector();
	const auto definition = std::find_if(perks.begin(), perks.end(), [&](const JsonNode & perk)
	{
		return perk["id"].String() == perkIdentifier;
	});
	if(definition == perks.end() || (*definition)["effect"]["status"].String() != "active")
		return false;

	hero->applyPerkSelection({std::string(newHorizonsBulwark::SKILL_ID), perkIdentifier});
	return hero->hasActivePerk(std::string(newHorizonsBulwark::SKILL_ID), perkIdentifier);
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
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 100);
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

	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	ASSERT_EQ(server.attacks.size(), 1u);
	ASSERT_EQ(server.attacks.front().bsa.size(), 1u);
	EXPECT_GT(server.attacks.front().bsa.front().damageAmount, defenderHealthBefore);
	EXPECT_FALSE(defender->alive());
	EXPECT_EQ(attackerHealthBefore - attacker->getAvailableHealth(),
		newHorizonsBulwark::reflectedDamage(defenderHealthBefore, 5000));
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
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(leftHex), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
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
	auto * magog = addStack(BattleSide::ATTACKER, creatureByName("core:magog"), BattleHex(leftHex), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
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
