/*
 * NewHorizonsDisciplineTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"

namespace
{
JsonNode certainMorale()
{
	JsonNode result;
	for(int i = 0; i < 10; ++i)
		result.Vector().emplace_back(100);
	return result;
}

class NewHorizonsDisciplineTest : public BattleTestFixture
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
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::COMBAT_GOOD_MORALE_CHANCE, certainMorale());
		loaded->overrideGameSetting(EGameSettings::COMBAT_MORALE_DICE_SIZE, JsonNode(100));
	}

	void prepare(bool selectPerk)
	{
		startGame();
		ASSERT_EQ(gameState()->getSettings().getVector(EGameSettings::COMBAT_GOOD_MORALE_CHANCE).front(), 100);
		ASSERT_EQ(gameState()->getSettings().getInteger(EGameSettings::COMBAT_MORALE_DICE_SIZE), 100);
		const auto decoded = SecondarySkill::decode("new-horizons:discipline");
		ASSERT_GE(decoded, 0);
		const SecondarySkill discipline(decoded);
		attackerSideHero->setSecSkillLevel(discipline, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		if(selectPerk)
			attackerSideHero->applyPerkSelection({
				"new-horizons:discipline", "new-horizons:discipline.inspirationalLeader"});

		startBattle();
		attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"),
			BattleHex(leftHex), 2);
		defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"),
			BattleHex(rightHex), 100);
		blockRetaliation(attacker);
		attacker->addNewBonus(std::make_shared<Bonus>(
			BonusDuration::PERMANENT, BonusType::MORALE, BonusSource::OTHER, 1, BonusSourceID()));
		beginCombat();
	}

	CStack * attacker = nullptr;
	CStack * defender = nullptr;
};
}

TEST_F(NewHorizonsDisciplineTest, InspirationalLeaderAddsDamageAfterPositiveMorale)
{
	prepare(true);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(
		"new-horizons:discipline", "new-horizons:discipline.inspirationalLeader"));
	EXPECT_GE(attacker->moraleVal(), 1);
	EXPECT_FALSE(attacker->hadMorale);
	EXPECT_FALSE(attacker->defending);
	EXPECT_FALSE(attacker->waited());
	EXPECT_FALSE(attacker->fear);
	EXPECT_TRUE(attacker->alive());
	EXPECT_TRUE(attacker->canMove());
	EXPECT_EQ(battle()->battleActiveUnit(), attacker);

	const BattleAttackInfo attackInfo(attacker, defender, 0, false);
	const auto baseDamage = battle()->calculateDmgRange(attackInfo).damage.min;
	ASSERT_GT(baseDamage, 0);
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	EXPECT_FALSE(server.attacks.empty());
	EXPECT_TRUE(vstd::contains_if(server.stackActivations, [](const BattleSetActiveStack & activation)
	{
		return activation.reason == BattleUnitTurnReason::MORALE;
	}));

	EXPECT_EQ(attacker->valOfBonuses(
		BonusType::PERCENTAGE_DAMAGE_BOOST,
		BonusSubtypeID(BonusCustomSubtype::damageTypeMelee)), 10);
	EXPECT_EQ(attacker->valOfBonuses(
		BonusType::PERCENTAGE_DAMAGE_BOOST,
		BonusSubtypeID(BonusCustomSubtype::damageTypeRanged)), 10);
	EXPECT_GT(battle()->calculateDmgRange(attackInfo).damage.min, baseDamage);

	// The bonus is scoped to the morale follow-up activation. Once that
	// activation's accepted action completes, the authoritative expiry path
	// removes both damage subtypes.
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	EXPECT_EQ(attacker->valOfBonuses(
		BonusType::PERCENTAGE_DAMAGE_BOOST,
		BonusSubtypeID(BonusCustomSubtype::damageTypeMelee)), 0);
	EXPECT_EQ(attacker->valOfBonuses(
		BonusType::PERCENTAGE_DAMAGE_BOOST,
		BonusSubtypeID(BonusCustomSubtype::damageTypeRanged)), 0);
}

TEST_F(NewHorizonsDisciplineTest, UnselectedInspirationalLeaderDoesNotGrantDamage)
{
	prepare(false);
	ASSERT_FALSE(attackerSideHero->hasActivePerk(
		"new-horizons:discipline", "new-horizons:discipline.inspirationalLeader"));
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	EXPECT_EQ(attacker->valOfBonuses(
		BonusType::PERCENTAGE_DAMAGE_BOOST,
		BonusSubtypeID(BonusCustomSubtype::damageTypeMelee)), 0);
	EXPECT_EQ(attacker->valOfBonuses(
		BonusType::PERCENTAGE_DAMAGE_BOOST,
		BonusSubtypeID(BonusCustomSubtype::damageTypeRanged)), 0);
}

TEST_F(NewHorizonsDisciplineTest, BattleMoraleInfoIncludesSteadfastStandardBearerAndOpeningModifier)
{
	startGame();
	const auto disciplineId = std::string("new-horizons:discipline");
	const auto decoded = SecondarySkill::decode(disciplineId);
	ASSERT_GE(decoded, 0);
	const SecondarySkill discipline(decoded);
	attackerSideHero->setSecSkillLevel(discipline, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({disciplineId, "new-horizons:discipline.steadfast"});
	attackerSideHero->setSecSkillLevel(discipline, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({disciplineId, "new-horizons:discipline.standardBearer"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(disciplineId, "new-horizons:discipline.steadfast"));
	ASSERT_TRUE(attackerSideHero->hasActivePerk(disciplineId, "new-horizons:discipline.standardBearer"));

	const int currentDay = gameState()->getCalendar().getCurrentDay();
	attackerSideHero->setNewHorizonsForcedMarchState(currentDay, currentDay);
	startBattle();
	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(7, 5), 10);
	addStack(BattleSide::ATTACKER, creatureByName("core:sprite"), BattleHex(8, 5), 10);
	addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 10);
	ASSERT_NE(target, nullptr);

	const auto ordinaryMorale = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MORALE, BonusSource::OTHER, -3, BonusSourceID());
	target->addNewBonus(ordinaryMorale);
	const auto hostileMorale = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MORALE, BonusSource::SPELL_EFFECT, -2, BonusSourceID());
	hostileMorale->appliedByEnemy = true;
	target->addNewBonus(hostileMorale);

	beginCombat();
	ASSERT_EQ(battle()->getRound(), 1);
	ASSERT_EQ(battle()->getFirstRoundMoraleModifier(BattleSide::ATTACKER), -1);
	const auto moraleInfo = battle()->battleGetMoraleInfo(target);
	EXPECT_EQ(moraleInfo.steadfastAdjustment, 1);
	EXPECT_EQ(moraleInfo.standardBearerBonus, 1);
	EXPECT_EQ(moraleInfo.firstRoundModifier, -1);
	EXPECT_EQ(moraleInfo.real, target->moraleValWithBonus(
		moraleInfo.steadfastAdjustment + moraleInfo.standardBearerBonus + moraleInfo.firstRoundModifier));
	EXPECT_NE(moraleInfo.real, target->moraleVal())
		<< "The readback includes the current hostile-effect, adjacency and first-round modifiers";
	EXPECT_FALSE(moraleInfo.commandingPresenceFloorApplied);
	EXPECT_FALSE(moraleInfo.furyUnboundFloorApplied);
}
