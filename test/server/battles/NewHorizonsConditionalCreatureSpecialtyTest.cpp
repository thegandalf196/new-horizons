/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "FullGameSnapshotTypes.h"
#include "../../serializer/CapturedSpecialtySnapshotFixture.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/NewHorizonsSpecialtyEvents.h"
#include "../../../lib/battle/NewHorizonsSpecialtyProcs.h"
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
			for(const char * key : {"massedSpearsDamagePercent", "packPursuitSpeed", "bloodScentDamagePercent",
				"undertowDefensePerAttack", "wyvernVenomPolicyVersion", "goblinFrenzyDamagePercent"})
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
TEST_P(NewHorizonsConditionalCreatureSpecialtyTest, AdmissionRejectsMalformedPolicyBeforeRepeatedTrustedQueries)
{
	prepare();
	const auto & captured = attackerSideHero->getPrimaryGrowthRules();
	ASSERT_NO_THROW(newHorizonsHeroes::validateResolvedHeroRules(captured));
	for(int query = 0; query < 1024; ++query)
		EXPECT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));

	auto invalid = captured;
	invalid["additionalSpecialties"]["ordinaryCreatureSpecialties"]["packPursuitSpeed"].Integer() = 6;
	newHorizonsHeroes::SpecialtyPresentation presentation;
	presentation.name = "unchanged";
	EXPECT_THROW(newHorizonsHeroes::resolveOrdinaryCreatureSpecialtyPresentation(
		*attackerSideHero->getHeroType(), invalid, presentation), std::runtime_error);
	EXPECT_EQ(presentation.name, "unchanged");
	EXPECT_THROW(newHorizonsHeroes::validateResolvedHeroRules(invalid), std::runtime_error);

	// Feed the real owner reader a constructed malformed current snapshot.
	auto bytes = capturedSpecialtySnapshotTest::constructedSnapshot(*attackerSideHero, captured, invalid);
	CMemorySerializer wire(std::move(bytes));
	wire.iser.loadingGamestate = true;
	wire.iser.cb = gameState().get();
	auto rejected = std::make_shared<CGHeroInstance>(gameState().get());
	EXPECT_THROW(rejected->serialize(wire.iser), std::runtime_error);
	EXPECT_EQ(attackerSideHero->getPrimaryGrowthRules(), captured);
	EXPECT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
}

TEST_P(NewHorizonsConditionalCreatureSpecialtyTest, SettingsRejectMalformedMergedPolicyAtomically)
{
	prepare();
	JsonNode input;
	input["heroes"]["newHorizons"] = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
	GameSettings settings;
	settings.loadBase(input);
	const auto original = settings.getValue(EGameSettings::HEROES_NEW_HORIZONS);
	const auto settingsSnapshot = [&settings]
	{
		CMemorySerializer serializer;
		serializer.oser & settings;
		return serializer.extractBuffer();
	};
	const auto overrides = settingsSnapshot();
	auto bad = original;
	bad["additionalSpecialties"]["ordinaryCreatureSpecialties"]["packPursuitSpeed"].Integer() = 6;
	EXPECT_THROW(settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, bad), std::runtime_error);
	EXPECT_EQ(settings.getValue(EGameSettings::HEROES_NEW_HORIZONS), original);
	EXPECT_EQ(settingsSnapshot(), overrides);

	auto badInput = input;
	badInput["heroes"]["newHorizons"] = bad;
	badInput["combat"]["moraleExtraDamagePercent"].Integer() = 77;
	EXPECT_THROW(settings.loadOverrides(badInput), std::runtime_error);
	EXPECT_EQ(settingsSnapshot(), overrides);
	EXPECT_EQ(settings.getValue(EGameSettings::HEROES_NEW_HORIZONS), original);
	EXPECT_THROW(settings.loadBase(badInput), std::runtime_error);
	EXPECT_EQ(settings.getValue(EGameSettings::HEROES_NEW_HORIZONS), original);

	// Valid partial overrides still merge with admitted definitions.
	JsonNode partial;
	partial["additionalSpecialties"]["ordinaryCreatureSpecialties"]["packPursuitSpeed"].Integer() = 5;
	EXPECT_NO_THROW(settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, partial));
	EXPECT_EQ(settings.getValue(EGameSettings::HEROES_NEW_HORIZONS), original);
}

TEST_P(NewHorizonsConditionalCreatureSpecialtyTest, FamilyPrimaryMeleeAndTypedReactionBoundaries)
{
	const auto [actor, victim] = combat();
	BattleAttackInfo attack(actor, victim, 0, false);
	const int expectedDamage = std::string(std::get<0>(GetParam())) == "core:fiona" ? 0 : 20;
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureAttackPercent(attackerSideHero, attack), expectedDamage);
	attack.retaliation = true;
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureAttackPercent(attackerSideHero, attack), expectedDamage);
	for(int exclusion = 0; exclusion < 5; ++exclusion)
	{
		auto excluded = attack;
		if(exclusion == 0) excluded.shooting = true;
		if(exclusion == 1) excluded.secondaryAttack = true;
		if(exclusion == 2) excluded.bracePreemptive = true;
		if(exclusion == 3) excluded.preemptiveDamagePercent = 50;
		if(exclusion == 4) excluded.physicalDamage = false;
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureAttackPercent(attackerSideHero, excluded), 0);
	}
	auto * unrelated = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex - 3), 100);
	attack.attacker = unrelated;
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureAttackPercent(attackerSideHero, attack), 0);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureAttackPercent(defenderSideHero, attack), 0);
}
TEST_P(NewHorizonsConditionalCreatureSpecialtyTest, LivePredicateUsesCurrentCountPositionOrRealTopWound)
{
	const auto [actor, victim] = combat();
	BattleAttackInfo attack(actor, victim, 0, false);
	const auto key = std::string(std::get<0>(GetParam()));
	if(key == "core:shakti")
	{
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureAttackPercent(attackerSideHero, attack), 20);
		int64_t casualty = actor->getMaxHealth();
		actor->health.damage(casualty);
		EXPECT_EQ(actor->getCount(), victim->getCount());
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureAttackPercent(attackerSideHero, attack), 0);
	}
	else if(key == "core:fiona")
	{
		const auto speed = actor->getBonuses(Selector::type()(BonusType::STACKS_SPEED).And(
			Selector::source(BonusSource::HERO_SPECIAL, BonusSourceID(attackerSideHero->getHeroTypeID()))));
		ASSERT_EQ(speed->size(), 1u);
		EXPECT_EQ(speed->front()->val, 5);
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
		auto * friend1 = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(rightHex + 1), 1);
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
		auto * friend2 = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(rightHex + 17), 1);
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
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
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureAttackPercent(attackerSideHero, attack), 20);
		int64_t finishWoundedTop = victim->getFirstHPleft();
		victim->health.damage(finishWoundedTop);
		EXPECT_LT(victim->getCount(), 9);
		EXPECT_EQ(victim->getFirstHPleft(), victim->getMaxHealth());
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureAttackPercent(attackerSideHero, attack), 0)
			<< "Casualties with a full next top creature must not impersonate a top wound";
		auto * undead = addStack(BattleSide::DEFENDER, creatureByName("core:skeleton"), BattleHex(rightHex + 5), 10);
		int64_t wound = 1;
		undead->health.damage(wound);
		attack.defender = undead;
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureAttackPercent(attackerSideHero, attack), 0);
		auto * construct = addStack(BattleSide::DEFENDER, creatureByName("core:ironGolem"), BattleHex(rightHex + 7), 10);
		wound = 1;
		construct->health.damage(wound);
		attack.defender = construct;
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureAttackPercent(attackerSideHero, attack), 0);
	}
}
INSTANTIATE_TEST_SUITE_P(ThreeSpecialtyIdentities, NewHorizonsConditionalCreatureSpecialtyTest, ::testing::Values(
	Identity{"core:shakti", "core:troglodyte", "core:infernalTroglodyte", "massedSpearsDamagePercent", "Massed Spears"},
	Identity{"core:fiona", "core:hellHound", "core:cerberus", "packPursuitSpeed", "Pack Pursuit"},
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
TEST_F(NewHorizonsNeighborAttackSpecialtyTest, UndertowEarnsDefenseWithoutAnAdjacentAlly)
{
	prepareIdentity({"core:lacus", "core:waterElemental", "core:iceElemental", "undertowDefensePerAttack", "Undertow"});
	startBattle();
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:iceElemental"), BattleHex(leftHex), 10);
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 10);
	BattleAttackInfo incoming(victim, actor, 0, false);
	const auto plans = newHorizonsSpecialtyEvents::acceptedStrike(*battle(), incoming);
	ASSERT_EQ(plans.size(), 1);
	ASSERT_EQ(plans.front().target, actor->unitId());
	ASSERT_EQ(plans.front().add.size(), 1);
	EXPECT_EQ(plans.front().add.front().val, 1);
	BattleAttackInfo counter(actor, victim, 0, false);
	counter.retaliation = true;
	addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex + 1), 1);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), counter), 100);
}
TEST_F(NewHorizonsNeighborAttackSpecialtyTest, GoblinFrenzyDoesNotDependOnAnAdjacentGoblin)
{
	prepareIdentity({"core:gretchin", "core:goblin", "core:hobgoblin", "goblinFrenzyDamagePercent", "Goblin Frenzy"});
	startBattle();
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:hobgoblin"), BattleHex(leftHex), 10);
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 10);
	BattleAttackInfo attack(actor, victim, 0, false);
	EXPECT_EQ(newHorizonsSpecialtyEvents::firstStrikeFinalMultiplier(*battle(), attack), 150);
	addStack(BattleSide::ATTACKER, creatureByName("core:goblin"), BattleHex(rightHex + 17), 1);
	EXPECT_EQ(newHorizonsSpecialtyEvents::firstStrikeFinalMultiplier(*battle(), attack), 150);
	const auto plans = newHorizonsSpecialtyEvents::acceptedStrike(*battle(), attack);
	ASSERT_EQ(plans.size(), 1);
	for(const auto & bonus : plans.front().add) actor->addNewBonus(std::make_shared<Bonus>(bonus));
	EXPECT_EQ(newHorizonsSpecialtyEvents::firstStrikeFinalMultiplier(*battle(), attack), 100);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
}
TEST_F(NewHorizonsNeighborAttackSpecialtyTest, WyvernsPreyGrantsNativePoisonWithoutAnIsolatedTargetPremium)
{
	prepareIdentity({"core:broghild", "core:wyvern", "core:wyvernMonarch", "wyvernVenomPolicyVersion", "Wyvern's Prey"});
	startBattle();
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:wyvernMonarch"), BattleHex(leftHex), 10);
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 10);
	BattleAttackInfo attack(actor, victim, 0, false);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex + 17), 1);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), attack), 100);
	EXPECT_EQ(newHorizonsSpecialtyProcs::poisonPotencyMultiplier(*battle(), *actor), 2);
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
	Identity{"core:lacus", "core:waterElemental", "core:iceElemental", "undertowDefensePerAttack", "Undertow"},
	Identity{"core:broghild", "core:wyvern", "core:wyvernMonarch", "wyvernVenomPolicyVersion", "Wyvern's Prey"},
	Identity{"core:gretchin", "core:goblin", "core:hobgoblin", "goblinFrenzyDamagePercent", "Goblin Frenzy"}));

}
