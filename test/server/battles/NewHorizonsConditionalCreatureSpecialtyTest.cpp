/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/bonuses/Bonus.h"
#include <tuple>

namespace
{
using Identity = std::tuple<const char *, const char *, const char *, const char *, const char *>;
class NewHorizonsConditionalCreatureSpecialtyTest : public HeroCommandFixture,
	public ::testing::WithParamInterface<Identity>
{
protected:
	bool historical = false;
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(historical)
			for(const char * key : {"massedSpearsDamagePercent", "packPursuitDamagePercent", "bloodScentDamagePercent",
				"undertowRetaliationPercent", "wyvernsPreyDamagePercent", "mobPressureDamagePercent"})
				rules["additionalSpecialties"]["ordinaryCreatureSpecialties"].Struct().erase(key);
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void prepare() { prepareIdentity(GetParam()); }
	void prepareIdentity(const Identity & identity)
	{
		const auto & [hero, base, upgrade, coefficient, title] = identity;
		const auto creature = [](const char * key) { return CreatureID(CreatureID::decode(key)); };
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(hero)), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{creature(base), 10}, {creature(upgrade), 10}, {creature("core:pikeman"), 1}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{creature("core:pikeman"), 10}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
	}
	std::pair<CStack *, CStack *> combat()
	{
		prepare();
		startBattle();
		auto * actor = addStack(BattleSide::ATTACKER, creatureByName(std::get<2>(GetParam())), BattleHex(leftHex), 10);
		auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 9);
		if(std::string(std::get<0>(GetParam())) == "core:vokial")
		{
			int64_t wound = 1;
			victim->health.damage(wound);
		}
		return {actor, victim};
	}
};
TEST_P(NewHorizonsConditionalCreatureSpecialtyTest, CapturedIdentityAndHistoricalAbsenceAreDistinct)
{
	historical = true;
	prepare();
	EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	EXPECT_NE(attackerSideHero->getSpecialtyPresentation().name, std::get<4>(GetParam()));
	CGameState restored;
	const auto id = attackerSideHero->id;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = restored.getHero(id);
	ASSERT_NE(loaded, nullptr);
	EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(loaded));
	EXPECT_EQ(loaded->getPrimaryGrowthRules(), attackerSideHero->getPrimaryGrowthRules());
}
TEST_P(NewHorizonsConditionalCreatureSpecialtyTest, CurrentIdentityAndDevelopmentSurviveWorldRoundtrip)
{
	prepare();
	EXPECT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	EXPECT_EQ(attackerSideHero->getSpecialtyPresentation().name, std::get<4>(GetParam()));
	EXPECT_EQ(attackerSideHero->getSpecialtyPresentation().description.find("%VALUE%"), std::string::npos);
	CGameState restored;
	const auto id = attackerSideHero->id;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = restored.getHero(id);
	ASSERT_NE(loaded, nullptr);
	EXPECT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(loaded));
	EXPECT_EQ(loaded->secSkills, attackerSideHero->secSkills);
	EXPECT_EQ(loaded->getPrimaryGrowthRules(), attackerSideHero->getPrimaryGrowthRules());
}
TEST_P(NewHorizonsConditionalCreatureSpecialtyTest, FamilyPrimaryMeleeAndTypedReactionBoundaries)
{
	const auto [actor, victim] = combat();
	BattleAttackInfo attack(actor, victim, 0, false);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureAttackPercent(attackerSideHero, attack, true), 20);
	attack.retaliation = true;
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureAttackPercent(attackerSideHero, attack, true), 20);
	for(int exclusion = 0; exclusion < 5; ++exclusion)
	{
		auto excluded = attack;
		if(exclusion == 0) excluded.shooting = true;
		if(exclusion == 1) excluded.secondaryAttack = true;
		if(exclusion == 2) excluded.bracePreemptive = true;
		if(exclusion == 3) excluded.preemptiveDamagePercent = 50;
		if(exclusion == 4) excluded.physicalDamage = false;
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureAttackPercent(attackerSideHero, excluded, true), 0);
	}
	auto * unrelated = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex - 3), 100);
	attack.attacker = unrelated;
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureAttackPercent(attackerSideHero, attack, true), 0);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureAttackPercent(defenderSideHero, attack, true), 0);
}
TEST_P(NewHorizonsConditionalCreatureSpecialtyTest, LivePredicateUsesCurrentCountPositionOrRealTopWound)
{
	const auto [actor, victim] = combat();
	BattleAttackInfo attack(actor, victim, 0, false);
	const auto key = std::string(std::get<0>(GetParam()));
	if(key == "core:shakti")
	{
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureAttackPercent(attackerSideHero, attack, false), 20);
		int64_t casualty = actor->getMaxHealth();
		actor->health.damage(casualty);
		EXPECT_EQ(actor->getCount(), victim->getCount());
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureAttackPercent(attackerSideHero, attack, false), 0);
	}
	else if(key == "core:fiona")
	{
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
		auto * friend1 = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(rightHex + 1), 1);
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 120);
		auto * friend2 = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(rightHex + 17), 1);
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 120);
		int64_t first = friend1->getAvailableHealth();
		friend1->health.damage(first);
		int64_t second = friend2->getAvailableHealth();
		friend2->health.damage(second);
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
		addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex + 1), 1);
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
	}
	else
	{
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureAttackPercent(attackerSideHero, attack, false), 20);
		int64_t finishWoundedTop = victim->getFirstHPleft();
		victim->health.damage(finishWoundedTop);
		EXPECT_LT(victim->getCount(), 9);
		EXPECT_EQ(victim->getFirstHPleft(), victim->getMaxHealth());
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureAttackPercent(attackerSideHero, attack, false), 0)
			<< "Casualties with a full next top creature must not impersonate a top wound";
		auto * undead = addStack(BattleSide::DEFENDER, creatureByName("core:skeleton"), BattleHex(rightHex + 5), 10);
		int64_t wound = 1;
		undead->health.damage(wound);
		attack.defender = undead;
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureAttackPercent(attackerSideHero, attack, false), 0);
		auto * construct = addStack(BattleSide::DEFENDER, creatureByName("core:ironGolem"), BattleHex(rightHex + 7), 10);
		wound = 1;
		construct->health.damage(wound);
		attack.defender = construct;
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureAttackPercent(attackerSideHero, attack, false), 0);
	}
}
INSTANTIATE_TEST_SUITE_P(ThreeConditionalIdentities, NewHorizonsConditionalCreatureSpecialtyTest, ::testing::Values(
	Identity{"core:shakti", "core:troglodyte", "core:infernalTroglodyte", "massedSpearsDamagePercent", "Massed Spears"},
	Identity{"core:fiona", "core:hellHound", "core:cerberus", "packPursuitDamagePercent", "Pack Pursuit"},
	Identity{"core:vokial", "core:vampire", "core:vampireLord", "bloodScentDamagePercent", "Blood Scent"}));
class NewHorizonsNeighborAttackSpecialtyTest : public NewHorizonsConditionalCreatureSpecialtyTest
{
protected:
	static std::shared_ptr<Bonus> control(CStack * unit)
	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED,
			BonusSource::OTHER, 1, BonusSourceID());
		unit->addNewBonus(bonus);
		return bonus;
	}
};
TEST_F(NewHorizonsNeighborAttackSpecialtyTest, UndertowRequiresCounterAndSourceNeighbor)
{
	prepareIdentity({"core:lacus", "core:waterElemental", "core:iceElemental", "undertowRetaliationPercent", "Undertow"});
	startBattle();
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:iceElemental"), BattleHex(leftHex), 10);
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 10);
	BattleAttackInfo attack(actor, victim, 0, false);
	attack.retaliation = true;
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
	auto * neighbor = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex + 1), 1);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 125);
	const auto controlled = control(neighbor);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
	neighbor->removeBonus(controlled);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 125);
	attack.retaliation = false;
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
	attack.retaliation = true;
	attack.preemptiveDamagePercent = 50;
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
}
TEST_F(NewHorizonsNeighborAttackSpecialtyTest, MobPressureRequiresDifferentFriendlyGoblinAtVictim)
{
	prepareIdentity({"core:gretchin", "core:goblin", "core:hobgoblin", "mobPressureDamagePercent", "Mob Pressure"});
	startBattle();
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:hobgoblin"), BattleHex(leftHex), 10);
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 10);
	BattleAttackInfo attack(actor, victim, 0, false);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
	addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(rightHex + 1), 1);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
	auto * neighbor = addStack(BattleSide::ATTACKER, creatureByName("core:goblin"), BattleHex(rightHex + 17), 1);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 120);
	const auto controlled = control(neighbor);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
	neighbor->removeBonus(controlled);
	attack.secondaryAttack = true;
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
}
TEST_F(NewHorizonsNeighborAttackSpecialtyTest, WyvernsPreyTestsVictimsOwnOrdinaryNeighbors)
{
	prepareIdentity({"core:broghild", "core:wyvern", "core:wyvernMonarch", "wyvernsPreyDamagePercent", "Wyvern's Prey"});
	startBattle();
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:wyvernMonarch"), BattleHex(leftHex), 10);
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 10);
	BattleAttackInfo attack(actor, victim, 0, false);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 120);
	addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(rightHex + 1), 1);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 120);
	auto * friend1 = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex + 17), 1);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
	const auto controlled = control(friend1);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 120);
	friend1->removeBonus(controlled);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
	int64_t health = friend1->getAvailableHealth();
	friend1->health.damage(health);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 120);
	attack.shooting = true;
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
}

TEST_P(NewHorizonsNeighborAttackSpecialtyTest, CapturedOptionalIdentitySurvivesWorldRoundtrip)
{
	prepareIdentity(GetParam());
	ASSERT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	EXPECT_EQ(attackerSideHero->getSpecialtyPresentation().name, std::get<4>(GetParam()));
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = restored.getHero(attackerSideHero->id);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->getPrimaryGrowthRules(), attackerSideHero->getPrimaryGrowthRules());
	EXPECT_EQ(loaded->secSkills, attackerSideHero->secSkills);
	EXPECT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(loaded));
}
TEST_P(NewHorizonsNeighborAttackSpecialtyTest, MissingOptionalIdentityRetainsHistoricalGenericSpecialty)
{
	historical = true;
	prepareIdentity(GetParam());
	EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = restored.getHero(attackerSideHero->id);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->getPrimaryGrowthRules(), attackerSideHero->getPrimaryGrowthRules());
	EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(loaded));
}
INSTANTIATE_TEST_SUITE_P(ThreeNeighborAttackIdentities, NewHorizonsNeighborAttackSpecialtyTest, ::testing::Values(
	Identity{"core:lacus", "core:waterElemental", "core:iceElemental", "undertowRetaliationPercent", "Undertow"},
	Identity{"core:broghild", "core:wyvern", "core:wyvernMonarch", "wyvernsPreyDamagePercent", "Wyvern's Prey"},
	Identity{"core:gretchin", "core:goblin", "core:hobgoblin", "mobPressureDamagePercent", "Mob Pressure"}));

}
