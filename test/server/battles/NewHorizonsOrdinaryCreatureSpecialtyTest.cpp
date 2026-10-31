/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/NewHorizonsArmorer.h"
#include "../../../lib/battle/NewHorizonsArchery.h"
#include "../../../lib/battle/UnitMagicResistanceQuery.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "FullGameSnapshotTypes.h"
#include "../../../lib/callback/GameRandomizer.h"
#include <tuple>
#include <set>
#include <vector>
#include "../../../client/windows/HeroSpecialtyPresentation.h"
#include "../../../lib/modding/IdentifierStorage.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/battle/NewHorizonsPuppetMaster.h"

namespace
{
using SpecialtyCase = std::tuple<const char *, const char *, const char *, BonusType, int, const char *, const char *>;
class NewHorizonsOrdinaryCreatureSpecialtyTest : public HeroCommandFixture,
	public ::testing::WithParamInterface<SpecialtyCase>
{
protected:
	bool historical = false;
	bool zeroCoefficients = false;
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(historical)
			rules["additionalSpecialties"].Struct().erase("ordinaryCreatureSpecialties");
		if(zeroCoefficients)
			for(const char * key : {"retaliationDamagePercent", "defendDefense", "maximumHealthPercent", "resistanceAuraPoints"})
				rules["additionalSpecialties"]["ordinaryCreatureSpecialties"][key].Integer() = 0;
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void prepare() { prepareCase(GetParam()); }
	void prepareCase(const SpecialtyCase & specialty)
	{
		const auto & [hero, base, upgrade, type, value, artwork, historicalArtwork] = specialty;
		const auto creature = [](const char * key) { return CreatureID(CreatureID::decode(key)); };
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(hero)), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{creature(base), 10}, {creature(upgrade), 10}, {creature("core:pikeman"), 10}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{creature(base), 10}, {creature(upgrade), 10}, {creature("core:pikeman"), 10}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
	}
	static void expectNativePair(const CGHeroInstance & hero, const char * stem)
	{
		const auto beforeRules = hero.getPrimaryGrowthRules();
		const auto beforeBonuses = hero.getExportedBonusList().toJsonNode();
		const std::string artwork = "newHorizons/heroSpecialties/" + std::string(stem);
		const auto metadata = hero.getSpecialtyPresentation();
		ASSERT_EQ(metadata.artwork, artwork);
		const auto card = heroSpecialtyPresentation(hero);
		EXPECT_EQ(card.animation().getOriginalName(), artwork + "/44");
		EXPECT_EQ(card.animation(true).getOriginalName(), artwork + "/32");
		EXPECT_EQ(card.frame(), 0u);
		// The prototype preview consumes an explicit context without a live hero.
		const auto preview = heroSpecialtyPresentation(*hero.getHeroType(), beforeRules,
			hero.getCreatureLineSpecialtyTarget(), hero.getMagicRules());
		EXPECT_EQ(preview.artwork, metadata.artwork);
		EXPECT_EQ(preview.name, metadata.name);
		EXPECT_EQ(preview.description, metadata.description);
		EXPECT_EQ(preview.animation().getOriginalName(), artwork + "/44");
		EXPECT_EQ(preview.animation(true).getOriginalName(), artwork + "/32");
		EXPECT_EQ(preview.frame(), 0u);
		EXPECT_EQ(hero.getPrimaryGrowthRules(), beforeRules);
		EXPECT_EQ(hero.getExportedBonusList().toJsonNode(), beforeBonuses);
	}
	static CSelector specialtySelector()
	{
		return CSelector([](const Bonus * bonus)
			{ return bonus && bonus->stacking.starts_with("new-horizons:ordinary-creature-specialty:"); });
	}
};
TEST_P(NewHorizonsOrdinaryCreatureSpecialtyTest, CurrentAndExplicitPreviewUseSelectedNativePair)
{
	prepare();
	ASSERT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	expectNativePair(*attackerSideHero, std::get<5>(GetParam()));
}
TEST_P(NewHorizonsOrdinaryCreatureSpecialtyTest, HistoricalAndExplicitPreviewRetainCreatureLineNativePair)
{
	historical = true;
	prepare();
	ASSERT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	expectNativePair(*attackerSideHero, std::get<6>(GetParam()));
}
TEST_P(NewHorizonsOrdinaryCreatureSpecialtyTest, ZeroCapturedCoefficientsRetainCurrentArtwork)
{
	zeroCoefficients = true;
	prepare();
	ASSERT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	expectNativePair(*attackerSideHero, std::get<5>(GetParam()));
}
TEST_P(NewHorizonsOrdinaryCreatureSpecialtyTest, AbsentConversionContextRetainsPrototypePresentation)
{
	prepare();
	const auto & prototype = *attackerSideHero->getHeroType();
	const auto metadata = newHorizonsHeroes::resolveSpecialtyPresentation(
		prototype, JsonNode(), JsonNode(), std::nullopt);
	EXPECT_FALSE(metadata.artwork);
	EXPECT_EQ(metadata.name, prototype.getSpecialtyNameTranslated());
	EXPECT_EQ(metadata.description, prototype.getSpecialtyDescriptionTranslated());
	const auto preview = heroSpecialtyPresentation(prototype, metadata);
	EXPECT_EQ(preview.animation().getOriginalName(), "UN44");
	EXPECT_EQ(preview.animation(true).getOriginalName(), "UN32");
	EXPECT_EQ(preview.frame(), static_cast<size_t>(prototype.imageIndex));
}
TEST_P(NewHorizonsOrdinaryCreatureSpecialtyTest, CurrentCaptureTargetsBothFormsOnlyAndRefreshIsIdempotent)
{
	prepare();
	const auto & [hero, base, upgrade, type, value, artwork, historicalArtwork] = GetParam();
	ASSERT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(defenderSideHero));
	for(int slot = 0; slot < 3; ++slot)
	{
		const auto * stack = attackerSideHero->getStackPtr(SlotID(slot));
		ASSERT_NE(stack, nullptr);
		const auto bonus = stack->getBonus(specialtySelector());
		if(slot == 2 || type == BonusType::NONE)
			EXPECT_FALSE(bonus);
		else
		{
			ASSERT_TRUE(bonus);
			EXPECT_EQ(bonus->type, type);
			EXPECT_EQ(bonus->val, value);
		}
		EXPECT_FALSE(defenderSideHero->getStackPtr(SlotID(slot))->getBonus(specialtySelector()));
	}
	const auto before = attackerSideHero->getExportedBonusList().size();
	attackerSideHero->initHero(*gameHandler->randomizer);
	attackerSideHero->initHero(*gameHandler->randomizer);
	EXPECT_EQ(attackerSideHero->getExportedBonusList().size(), before);
	const auto presentation = attackerSideHero->getSpecialtyPresentation();
	EXPECT_EQ(presentation.description.find("%VALUE%"), std::string::npos);
	EXPECT_TRUE(presentation.creature);
	EXPECT_EQ(std::ranges::count_if(attackerSideHero->getExportedBonusList(), [](const auto & bonus)
	{ return bonus && bonus->stacking.starts_with("new-horizons:ordinary-creature-specialty:"); }), type == BonusType::NONE ? 0 : 1);
	EXPECT_EQ(std::ranges::count_if(attackerSideHero->getExportedBonusList(), [](const auto & bonus)
	{ return bonus && bonus->stacking.starts_with("new-horizons:creature-line-specialty:"); }), 0);
}
TEST_P(NewHorizonsOrdinaryCreatureSpecialtyTest, CapturedAndHistoricalWorldRoundtripsRetainTheirIdentity)
{
	historical = true;
	prepare();
	EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	EXPECT_FALSE(attackerSideHero->getBonus(specialtySelector()));
	expectNativePair(*attackerSideHero, std::get<6>(GetParam()));
	const auto id = attackerSideHero->id;
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * oldHero = restored.getHero(id);
	ASSERT_NE(oldHero, nullptr);
	EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(oldHero));
	EXPECT_EQ(oldHero->getPrimaryGrowthRules(), attackerSideHero->getPrimaryGrowthRules());
	expectNativePair(*oldHero, std::get<6>(GetParam()));
}
TEST_P(NewHorizonsOrdinaryCreatureSpecialtyTest, CurrentWorldRoundtripPreservesPolicyBonusesAndStartingDevelopment)
{
	prepare();
	const auto & [key, base, upgrade, type, value, artwork, historicalArtwork] = GetParam();
	const auto id = attackerSideHero->id;
	expectNativePair(*attackerSideHero, artwork);
	const auto name = attackerSideHero->getSpecialtyPresentation().name;
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * hero = restored.getHero(id);
	ASSERT_NE(hero, nullptr);
	EXPECT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(hero));
	EXPECT_EQ(hero->getPrimaryGrowthRules(), attackerSideHero->getPrimaryGrowthRules());
	EXPECT_EQ(hero->secSkills, attackerSideHero->secSkills);
	EXPECT_EQ(hero->getSpecialtyPresentation().name, name);
	expectNativePair(*hero, artwork);
	EXPECT_EQ(std::ranges::count_if(hero->getExportedBonusList(), [](const auto & bonus)
	{ return bonus && bonus->stacking.starts_with("new-horizons:ordinary-creature-specialty:"); }), type == BonusType::NONE ? 0 : 1);
	for(int slot = 0; slot < 3; ++slot)
		EXPECT_EQ(hero->getStackPtr(SlotID(slot))->getCount(), attackerSideHero->getStackPtr(SlotID(slot))->getCount());
}

TEST_P(NewHorizonsOrdinaryCreatureSpecialtyTest, ExistingConsumersUseCapturedBonusWithoutGrantingOtherAbilities)
{
	prepare();
	startBattle();
	const auto & [hero, base, upgrade, type, value, artwork, historicalArtwork] = GetParam();
	auto * unit = addStack(BattleSide::ATTACKER, creatureByName(upgrade), BattleHex(leftHex), 10);
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
	ASSERT_NE(unit, nullptr);
	ASSERT_NE(victim, nullptr);
	if(type == BonusType::NO_MELEE_PENALTY || type == BonusType::NO_DISTANCE_PENALTY)
	{
		EXPECT_FALSE(unit->hasBonusOfType(BonusType::FREE_SHOOTING));
		EXPECT_FALSE(unit->hasBonusOfType(BonusType::NO_WALL_PENALTY));
		// Valeska keeps her accepted starting Point-Blank Shot perk. The
		// specialty changes melee damage, not the independent shooting rule.
		const bool pointBlank = newHorizonsArchery::hasPointBlankShot(attackerSideHero);
		EXPECT_EQ(pointBlank, type == BonusType::NO_MELEE_PENALTY);
		EXPECT_EQ(battle()->battleCanShoot(unit, victim->getPosition()), pointBlank);
		if(type == BonusType::NO_DISTANCE_PENALTY)
		{
			EXPECT_FALSE(unit->hasBonusOfType(BonusType::NO_MELEE_PENALTY));
			EXPECT_FALSE(battle()->battleHasDistancePenalty(unit, BattleHex(18), BattleHex(168)));
			attackerSideHero->removeBonuses(specialtySelector());
			EXPECT_TRUE(battle()->battleHasDistancePenalty(unit, BattleHex(18), BattleHex(168)));
			EXPECT_EQ(battle()->battleCanShoot(unit, victim->getPosition()), pointBlank);
		}
		else
		{
			BattleAttackInfo attack(unit, victim, 0, false);
			const auto trained = battle()->calculateDmgRange(attack);
			attackerSideHero->removeBonuses(specialtySelector());
			const auto ordinary = battle()->calculateDmgRange(attack);
			EXPECT_GT(trained.damage.max, ordinary.damage.max);
			EXPECT_EQ(battle()->battleCanShoot(unit, victim->getPosition()), pointBlank);
		}
	}
	if(type == BonusType::NONE)
	{
		BattleAttackInfo attack(unit, victim, 0, false);
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureRetaliationPercent(attackerSideHero, attack), 0);
		attack.retaliation = true;
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureRetaliationPercent(attackerSideHero, attack), value);
		const auto strengthened = battle()->calculateDmgRange(attack);
		attack.retaliation = false;
		const auto ordinary = battle()->calculateDmgRange(attack);
		// Ordinary range endpoints already floor; the independent multiplier precedes
		// that floor, so a fractional ordinary endpoint can add one extra point.
		EXPECT_GE(strengthened.damage.min, ordinary.damage.min * 125 / 100);
		EXPECT_LE(strengthened.damage.min, ordinary.damage.min * 125 / 100 + 1);
		EXPECT_GE(strengthened.damage.max, ordinary.damage.max * 125 / 100);
		EXPECT_LE(strengthened.damage.max, ordinary.damage.max * 125 / 100 + 1);
		// Fire Shield derives reflection from this attacker-side output; target
		// mitigation must not erase the specialty from its reflection basis.
		EXPECT_GE(strengthened.damageBeforeDefense.min, ordinary.damageBeforeDefense.min * 125 / 100);
		EXPECT_LE(strengthened.damageBeforeDefense.min, ordinary.damageBeforeDefense.min * 125 / 100 + 1);
		EXPECT_GE(strengthened.damageBeforeDefense.max, ordinary.damageBeforeDefense.max * 125 / 100);
		EXPECT_LE(strengthened.damageBeforeDefense.max, ordinary.damageBeforeDefense.max * 125 / 100 + 1);
		attack.retaliation = true;
		for(int exclusion = 0; exclusion < 5; ++exclusion)
		{
			auto excluded = attack;
			if(exclusion == 0) excluded.secondaryAttack = true;
			if(exclusion == 1) excluded.bracePreemptive = true;
			if(exclusion == 2) excluded.shooting = true;
			if(exclusion == 3) excluded.physicalDamage = false;
			if(exclusion == 4) excluded.preemptiveDamagePercent = 50;
			EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureRetaliationPercent(attackerSideHero, excluded), 0);
		}
		// Authority and AI both encode Battlecraft/Bulwark as counter=true
		// together with a positive typed pre-emptive percentage.
		auto preemptive = attack;
		preemptive.preemptiveDamagePercent = 50;
		const auto preemptiveWithCounter = battle()->calculateDmgRange(preemptive);
		preemptive.retaliation = false;
		const auto preemptiveWithoutCounter = battle()->calculateDmgRange(preemptive);
		EXPECT_EQ(preemptiveWithCounter.damage.min, preemptiveWithoutCounter.damage.min);
		EXPECT_EQ(preemptiveWithCounter.damage.max, preemptiveWithoutCounter.damage.max);
		EXPECT_EQ(preemptiveWithCounter.damageBeforeDefense.max, preemptiveWithoutCounter.damageBeforeDefense.max);
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureRetaliationPercent(defenderSideHero, attack), 0);
		auto * unrelated = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex - 2), 10);
		attack.attacker = unrelated;
		EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureRetaliationPercent(attackerSideHero, attack), 0);
	}
	if(type == BonusType::DEFENSIVE_STANCE)
	{
		const auto defenseSelector = Selector::typeSubtype(BonusType::PRIMARY_SKILL,
			BonusSubtypeID(PrimarySkill::DEFENSE));
		const int baseDefense = unit->valOfBonuses(defenseSelector);
		ASSERT_GT(baseDefense, 4); // This fixture does not use the weak-creature fallback.
		for(const auto & bonus : *unit->getBonuses(defenseSelector))
		{
			ASSERT_NE(bonus->valType, BonusValueType::PERCENT_TO_ALL);
			ASSERT_NE(bonus->valType, BonusValueType::INDEPENDENT_MIN);
			ASSERT_NE(bonus->valType, BonusValueType::INDEPENDENT_MAX);
			ASSERT_FALSE(bonus->valType == BonusValueType::PERCENT_TO_SOURCE && bonus->source == BonusSource::OTHER);
			ASSERT_FALSE(bonus->valType == BonusValueType::PERCENT_TO_TARGET_TYPE && bonus->targetSourceType == BonusSource::OTHER);
		}
		const int trainedStance = unit->valOfBonuses(BonusType::DEFENSIVE_STANCE);
		const auto trained = newHorizonsArmorer::buildDefendStance(unit);
		attackerSideHero->removeBonuses(specialtySelector());
		const int ordinaryStance = unit->valOfBonuses(BonusType::DEFENSIVE_STANCE);
		const auto ordinary = newHorizonsArmorer::buildDefendStance(unit);
		EXPECT_EQ(trainedStance - ordinaryStance, value);
		// The flat stance input participates in ordinary Defend's +20%, before
		// the standard integer floor. Its final difference need not equal +3.
		const int64_t expectedDifference = (static_cast<int64_t>(baseDefense) + trainedStance) * 120 / 100
			- (static_cast<int64_t>(baseDefense) + ordinaryStance) * 120 / 100;
		EXPECT_EQ(trained.defenseIncrease - ordinary.defenseIncrease, expectedDifference);
		for(const auto & bonus : trained.bonuses)
			EXPECT_EQ(bonus.duration, BonusDuration::STACK_GETS_TURN);
	}
	if(type == BonusType::STACK_HEALTH)
	{
		const auto maximum = unit->getMaxHealth();
		const auto count = unit->getCount();
		int64_t wound = 5;
		unit->health.damage(wound);
		const auto wounded = unit->getAvailableHealth();
		attackerSideHero->initHero(*gameHandler->randomizer);
		EXPECT_EQ(unit->getAvailableHealth(), wounded);
		EXPECT_EQ(unit->getCount(), count);
		attackerSideHero->removeBonuses(specialtySelector());
		EXPECT_GT(maximum, unit->getMaxHealth());
	}
	if(type == BonusType::SPELL_RESISTANCE_AURA)
	{
		class AuraQuery final : public battle::UnitMagicResistanceQuery
		{
			const battle::Unit * source;
			battle::Units adjacentUnits(const battle::Unit &) const override { return {source, source}; }
			bool newHorizonsRulesActive() const override { return true; }
		public:
			explicit AuraQuery(const battle::Unit * unit) : source(unit) {}
		} query(unit);
		const auto enhanced = unit->valOfBonuses(BonusType::SPELL_RESISTANCE_AURA);
		auto * recipient = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex - 2), 10);
		const auto protection = query.query(recipient);
		attackerSideHero->removeBonuses(specialtySelector());
		EXPECT_EQ(enhanced - unit->valOfBonuses(BonusType::SPELL_RESISTANCE_AURA), value);
		EXPECT_LE(protection, 75);
		EXPECT_GT(protection, query.query(recipient));
		recipient->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::MAGIC_RESISTANCE,
			BonusSource::OTHER, 100, BonusSourceID()));
		EXPECT_EQ(query.query(recipient), 75);
	}
}

INSTANTIATE_TEST_SUITE_P(SixIdentities, NewHorizonsOrdinaryCreatureSpecialtyTest, ::testing::Values(
	SpecialtyCase{"core:valeska", "core:archer", "core:marksman", BonusType::NO_MELEE_PENALTY, 1, "ValeskaCloseQuartersDrill", "ValeskaArcherLine"},
	SpecialtyCase{"core:wystan", "core:lizardman", "core:lizardWarrior", BonusType::NO_DISTANCE_PENALTY, 1, "WystanMarshSharpshooter", "WystanLizardmanLine"},
	SpecialtyCase{"core:dace", "core:minotaur", "core:minotaurKing", BonusType::NONE, 25, "DaceUnbrokenChallenge", "DaceMinotaurLine"},
	SpecialtyCase{"core:thunar", "core:earthElemental", "core:magmaElemental", BonusType::DEFENSIVE_STANCE, 3, "ThunarEarthshaper", "ThunarEarthElementalLine"},
	SpecialtyCase{"core:erdamon", "core:earthElemental", "core:magmaElemental", BonusType::STACK_HEALTH, 20, "ErdamonStonecaller", "ErdamonEarthElementalLine"},
	SpecialtyCase{"core:clancy", "core:unicorn", "core:warUnicorn", BonusType::SPELL_RESISTANCE_AURA, 10, "ClancyUnicornSanctuary", "ClancyUnicornLine"}));

class DaceOrdinaryRetaliationTest : public NewHorizonsOrdinaryCreatureSpecialtyTest {};
TEST_F(DaceOrdinaryRetaliationTest, AuthoritativeCounterAndScriptedFireShieldUseSameBoostedReflectionBasis)
{
	prepareCase({"core:dace", "core:minotaur", "core:minotaurKing", BonusType::NONE, 25,
		"DaceUnbrokenChallenge", "DaceMinotaurLine"});
	startBattle();
	auto * retaliator = addStack(BattleSide::ATTACKER, creatureByName("core:minotaurKing"), BattleHex(leftHex), 10);
	auto * shielded = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(rightHex), 1);
	ASSERT_NE(retaliator, nullptr);
	ASSERT_NE(shielded, nullptr);
	// A high-health, weak attacker makes both the ordinary counter and its
	// reflected damage observable without casualty or absorbed-health caps.
	shielded->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACK_HEALTH,
		BonusSource::OTHER, 10000, BonusSourceID()));
	shielded->health.init(); // authored full-health fixture after its explicit HP grant
	forceMaximumDamage(retaliator);
	forceMaximumDamage(shielded);
	// Four arguments select the typed lookup, not (scope, fullName, bool).
	const auto script = LIBRARY->identifiers()->getIdentifier(ModScope::scopeBuiltin(), "script", "fireShield", false);
	ASSERT_TRUE(script.has_value());
	ASSERT_EQ(ScriptID::encode(*script), "core:fireShield");
	shielded->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::COMBAT_EVENT_TRIGGER,
		BonusSource::OTHER, 100, BonusSourceID(), BonusSubtypeID(ScriptID(*script))));
	beginCombat();
	BattleAttackInfo incoming(shielded, retaliator, 0, false);
	const auto initial = battle()->calculateDmgRange(incoming);
	const auto counter = incoming.reverse();
	const auto boosted = battle()->calculateDmgRange(counter);
	ASSERT_GT(boosted.damage.max, 0);
	ASSERT_GT(boosted.damageBeforeDefense.max, 0);
	const auto reflection = SpellID(SpellID::FIRE_SHIELD).toSpell()->adjustRawDamage(shielded, retaliator,
		std::min(shielded->getAvailableHealth(), boosted.damageBeforeDefense.max));
	const auto before = retaliator->getAvailableHealth();
	ASSERT_TRUE(attack(shielded, retaliator->getPosition()));
	EXPECT_EQ(before - retaliator->getAvailableHealth(), initial.damage.max + reflection);
	EXPECT_TRUE(std::ranges::any_of(server.attacks, [retaliator](const auto & result)
	{ return result.stackAttacking == retaliator->unitId() && result.counter(); }));
}

TEST_F(DaceOrdinaryRetaliationTest, ActiveCounterUsesEffectiveArmyHeroAndNotPuppetActionController)
{
	prepareCase({"core:dace", "core:minotaur", "core:minotaurKing", BonusType::NONE, 25, nullptr, nullptr});
	startBattle();
	auto * native = addStack(BattleSide::ATTACKER, creatureByName("core:minotaurKing"), BattleHex(leftHex), 10);
	auto * opponent = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
	BattleAttackInfo counter(native, opponent, 0, false);
	counter.retaliation = true;
	const auto ordinaryCounter = battle()->calculateDmgRange(counter);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureRetaliationPercent(battle()->battleGetOwnerHero(native), counter), 25);
	const auto puppet = SpellID(SpellID::decode("new-horizons:puppetMaster"));
	native->addNewBonus(std::make_shared<Bonus>(newHorizonsPuppetMaster::controlMarker(puppet, PlayerColor(1))));
	EXPECT_EQ(battle()->battleGetActionController(native), PlayerColor(1));
	EXPECT_EQ(battle()->battleGetOwnerHero(native), attackerSideHero);
	const auto underPuppet = battle()->calculateDmgRange(counter);
	EXPECT_EQ(underPuppet.damage.min, ordinaryCounter.damage.min);
	EXPECT_EQ(underPuppet.damage.max, ordinaryCounter.damage.max);
	EXPECT_EQ(underPuppet.damageBeforeDefense.max, ordinaryCounter.damageBeforeDefense.max);
	native->removeBonuses(Selector::type()(BonusType::PUPPET_MASTER_CONTROL));
	native->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED,
		BonusSource::OTHER, 1, BonusSourceID()));
	auto * originalAlly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex - 2), 100);
	counter.defender = originalAlly;
	EXPECT_EQ(battle()->battleGetOwnerHero(native), defenderSideHero);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureRetaliationPercent(battle()->battleGetOwnerHero(native), counter), 0);
	auto * foreign = addStack(BattleSide::DEFENDER, creatureByName("core:minotaurKing"), BattleHex(rightHex + 2), 10);
	foreign->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED,
		BonusSource::OTHER, 1, BonusSourceID()));
	counter.attacker = foreign;
	counter.defender = opponent;
	EXPECT_EQ(battle()->battleGetOwnerHero(foreign), attackerSideHero);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureRetaliationPercent(battle()->battleGetOwnerHero(foreign), counter), 25);
	foreign->removeBonuses(Selector::type()(BonusType::HYPNOTIZED));
	foreign->addNewBonus(std::make_shared<Bonus>(newHorizonsPuppetMaster::controlMarker(puppet, PlayerColor(0))));
	counter.defender = originalAlly;
	EXPECT_EQ(battle()->battleGetActionController(foreign), PlayerColor(0));
	EXPECT_EQ(battle()->battleGetOwnerHero(foreign), defenderSideHero);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureRetaliationPercent(battle()->battleGetOwnerHero(foreign), counter), 0);
}

TEST_F(DaceOrdinaryRetaliationTest, ExtendedCapturedHeroPolicyRejects1144BeforeHeroPrefix)
{
	prepareCase({"core:dace", "core:minotaur", "core:minotaurKing", BonusType::NONE, 25, nullptr, nullptr});
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES;
	EXPECT_THROW(old.oser & *attackerSideHero, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}

TEST(NewHorizonsOrdinaryCreatureSpecialtyPolicy, CurrentSettingsRoundtripAndPreviousWriterAdmission)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	newHorizonsHeroes::validateOrdinaryCreatureSpecialtiesSerialization(rules, true);
	EXPECT_THROW(newHorizonsHeroes::validateOrdinaryCreatureSpecialtiesSerialization(rules, false), std::runtime_error);
	GameSettings settings;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, rules);
	CMemorySerializer memory;
	settings.serialize(memory.oser);
	GameSettings restored;
	restored.serialize(memory.iser);
	EXPECT_EQ(restored.getValue(EGameSettings::HEROES_NEW_HORIZONS), rules);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_SYLVAN_TACTICS;
	EXPECT_THROW(settings.serialize(old.oser), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	rules["additionalSpecialties"].Struct().erase("ordinaryCreatureSpecialties");
	EXPECT_NO_THROW(newHorizonsHeroes::validateOrdinaryCreatureSpecialtiesSerialization(rules, false));
}
TEST(NewHorizonsOrdinaryCreatureSpecialtyPolicy, InvalidAndUnknownCoefficientsReject)
{
	const JsonNode original(JsonPath::builtin("config/newHorizonsHeroes"));
	for(const char * key : {"retaliationDamagePercent", "defendDefense", "maximumHealthPercent", "resistanceAuraPoints"})
		for(int invalid : {-1, 101})
		{
			auto rules = original;
			rules["additionalSpecialties"]["ordinaryCreatureSpecialties"][key].Integer() = invalid;
			EXPECT_THROW(newHorizonsHeroes::validateOrdinaryCreatureSpecialtiesSerialization(rules, true), std::runtime_error);
		}
	auto rules = original;
	rules["additionalSpecialties"]["ordinaryCreatureSpecialties"]["unknown"].Integer() = 1;
	EXPECT_THROW(newHorizonsHeroes::validateOrdinaryCreatureSpecialtiesSerialization(rules, true), std::runtime_error);
}
TEST(NewHorizonsOrdinaryCreatureSpecialtyPolicy, FirstSix1144RetainsRoundtripAndEveryNewKeyRejectsBeforeSettingsPrefix)
{
	JsonNode firstSix(JsonPath::builtin("config/newHorizonsHeroes"));
	auto & firstSixPolicy = firstSix["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	const auto capturedPolicy = firstSixPolicy;
	const std::set<std::string> firstSixFields{"version", "closeQuartersDrill", "marshSharpshooter",
		"retaliationDamagePercent", "defendDefense", "maximumHealthPercent", "resistanceAuraPoints"};
	std::vector<std::string> laterKeys;
	for(const auto & entry : capturedPolicy.Struct())
		if(!firstSixFields.contains(entry.first))
			laterKeys.push_back(entry.first);
	for(const auto & key : laterKeys)
		firstSixPolicy.Struct().erase(key);
	ASSERT_EQ(firstSixPolicy.Struct().size(), firstSixFields.size());
	GameSettings oldSettings;
	oldSettings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, firstSix);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES;
	ASSERT_NO_THROW(oldSettings.serialize(old.oser));
	GameSettings restored;
	old.iser.version = ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES;
	ASSERT_NO_THROW(restored.serialize(old.iser));
	EXPECT_EQ(restored.getValue(EGameSettings::HEROES_NEW_HORIZONS), firstSix);
	for(const auto & key : laterKeys)
	{
		// These two capabilities have finite positive domains, not arbitrary percentages.
		const int positive = key == "rangefinderFullDamageDistance" || key == "ogreBulwarkRetaliations"
			? static_cast<int>(capturedPolicy[key].Integer()) : 20;
		const char * companion = key == "gunneryProfessorInitiativeFlat" ? "gunneryProfessorFollowUpBonusPoints"
			: key == "gunneryProfessorFollowUpBonusPoints" ? "gunneryProfessorInitiativeFlat" : nullptr;
		for(int value : {0, positive})
		{
			auto extended = firstSix;
			auto & policy = extended["additionalSpecialties"]["ordinaryCreatureSpecialties"];
			if(companion)
				policy[companion] = capturedPolicy[companion];
			policy[key].Integer() = value;
			GameSettings settings;
			settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, extended);
			CMemorySerializer unsupported;
			unsupported.oser.version = ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES;
			EXPECT_THROW(settings.serialize(unsupported.oser), std::runtime_error) << key;
			EXPECT_TRUE(unsupported.extractBuffer().empty()) << key;
			CMemorySerializer current;
			EXPECT_NO_THROW(settings.serialize(current.oser));
		}
	}
}

class PiquedramDefendSpecialtyTest : public NewHorizonsOrdinaryCreatureSpecialtyTest {};
TEST_F(PiquedramDefendSpecialtyTest, PhysicalRangedProtectionFollowsNativeArmyAndOrdinaryDefendLifetime)
{
	prepareCase({"core:piquedram", "core:stoneGargoyle", "core:obsidianGargoyle", BonusType::NONE, 20, nullptr, nullptr});
	startBattle();
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	auto * gargoyle = addStack(BattleSide::ATTACKER, creatureByName("core:obsidianGargoyle"), BattleHex(leftHex), 10);
	auto * archer = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(rightHex), 10);
	for(auto * unit : {gargoyle, archer})
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::NO_MORALE,
			BonusSource::OTHER, 1, BonusSourceID()));
	gargoyle->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
		BonusSource::OTHER, 50, BonusSourceID()));
	BattleAttackInfo shot(archer, gargoyle, 0, true);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureProtectionPercent(*battle(), shot), 0);
	beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), gargoyle);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(gargoyle),
		BattleAction::makeDefend(gargoyle)));
	ASSERT_TRUE(gargoyle->defended());
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureProtectionPercent(*battle(), shot), 20);
	auto melee = shot;
	melee.shooting = false;
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureProtectionPercent(*battle(), melee), 0);
	auto magical = shot;
	magical.physicalDamage = false;
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureProtectionPercent(*battle(), magical), 0);
	auto control = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED,
		BonusSource::OTHER, 1, BonusSourceID());
	gargoyle->addNewBonus(control);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureProtectionPercent(*battle(), shot), 20);
	gargoyle->removeBonus(control);
	for(int step = 0; step < 8 && battle()->battleActiveUnit() != gargoyle; ++step)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(active),
			BattleAction::makeDefend(active)));
	}
	ASSERT_EQ(battle()->battleActiveUnit(), gargoyle);
	EXPECT_FALSE(gargoyle->defended());
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureProtectionPercent(*battle(), shot), 0);
}
TEST_F(PiquedramDefendSpecialtyTest, CurrentPolicyAndStartingDevelopmentSurviveWorldRoundtrip)
{
	prepareCase({"core:piquedram", "core:stoneGargoyle", "core:obsidianGargoyle", BonusType::NONE, 20, nullptr, nullptr});
	ASSERT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	EXPECT_EQ(attackerSideHero->getSpecialtyPresentation().name, "Stone Sentinel");
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = restored.getHero(attackerSideHero->id);
	ASSERT_NE(loaded, nullptr);
	EXPECT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(loaded));
	EXPECT_EQ(loaded->secSkills, attackerSideHero->secSkills);
	EXPECT_EQ(loaded->getPrimaryGrowthRules(), attackerSideHero->getPrimaryGrowthRules());
}

TEST_F(PiquedramDefendSpecialtyTest, HistoricalAbsenceDoesNotAcquireReplacementOnWorldRoundtrip)
{
	historical = true;
	prepareCase({"core:piquedram", "core:stoneGargoyle", "core:obsidianGargoyle", BonusType::NONE, 20, nullptr, nullptr});
	EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = restored.getHero(attackerSideHero->id);
	ASSERT_NE(loaded, nullptr);
	EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(loaded));
	EXPECT_EQ(loaded->getPrimaryGrowthRules(), attackerSideHero->getPrimaryGrowthRules());
}

}
