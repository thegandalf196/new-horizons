/*
 * NewHorizonsMagicalDamageReductionTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt file in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

namespace
{
constexpr auto holyArmorRosterKey = "new-horizons:holyArmor";

std::shared_ptr<Bonus> spellDamageReduction(int value, SpellSchool school = SpellSchool::ANY)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELL_DAMAGE_REDUCTION,
		BonusSource::OTHER, value, BonusSourceID(), BonusSubtypeID(school));
}

class NewHorizonsMagicalDamageReductionTest : public HeroCommandFixture
{
protected:
	int rulesetVersion = newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION;
	bool includeHolyArmorRosterRow = true;
	CStack * target = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		if(rulesetVersion != newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
		{
			rules["rulesetVersion"].Integer() = rulesetVersion;
			rules.Struct().erase("schoolRankPowerCoefficientPercent");
			rules.Struct().erase("spellcraftEfficiencyPercent");
		}
		if(!includeHolyArmorRosterRow)
			rules["spells"].Struct().erase(holyArmorRosterKey);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}

	void prepareBattle()
	{
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
		attackerSideHero->addSpellToSpellbook(SpellID::FIREBALL);
		attackerSideHero->addSpellToSpellbook(SpellID::SLOW);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 20, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1000);
		beginCombat();
	}

	const CSpell * spell(SpellID id) const
	{
		return id.toSpell();
	}

};
}

TEST_F(NewHorizonsMagicalDamageReductionTest, IndependentAnySourcesMultiplyAndMatchForecastAndCast)
{
	prepareBattle();
	ASSERT_NE(target, nullptr);
	target->addNewBonus(spellDamageReduction(50));
	target->addNewBonus(spellDamageReduction(20));

	const auto * selectedSpell = spell(SpellID::MAGIC_ARROW);
	ASSERT_NE(selectedSpell, nullptr);
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, selectedSpell);
	const auto mechanics = selectedSpell->battleMechanics(&event);
	ASSERT_TRUE(mechanics->usesNewHorizonsMultiplicativeMDR());
	const auto rawDamage = mechanics->getEffectValue();
	ASSERT_GT(rawDamage, 0);
	const auto expectedDamage = rawDamage * 50 * 80 / 10000;
	const auto forecastDamage = mechanics->adjustEffectValue(target);
	EXPECT_EQ(forecastDamage, expectedDamage);

	const auto healthBefore = target->getAvailableHealth();
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::MAGIC_ARROW, target));
	EXPECT_EQ(healthBefore - target->getAvailableHealth(), expectedDamage)
		<< "The authoritative cast and spell forecast use the same independent-source product";
}

TEST_F(NewHorizonsMagicalDamageReductionTest, MatchingSchoolSourcesMultiplyAndPreserveFirstSchoolSelection)
{
	prepareBattle();
	ASSERT_NE(target, nullptr);
	target->addNewBonus(spellDamageReduction(50, SpellSchool::FIRE));
	target->addNewBonus(spellDamageReduction(20, SpellSchool::FIRE));

	const auto * selectedSpell = spell(SpellID::FIREBALL);
	ASSERT_NE(selectedSpell, nullptr);
	ASSERT_TRUE(selectedSpell->hasSchool(SpellSchool::FIRE));
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, selectedSpell);
	const auto mechanics = selectedSpell->battleMechanics(&event);
	const auto rawDamage = mechanics->getEffectValue();
	ASSERT_GT(rawDamage, 0);
	EXPECT_EQ(mechanics->adjustEffectValue(target), rawDamage * 50 * 80 / 10000)
		<< "Two reductions for the selected school multiply instead of summing to 70%";

	target->addNewBonus(spellDamageReduction(20));
	EXPECT_EQ(mechanics->adjustEffectValue(target), rawDamage * 50 * 80 * 80 / 1'000'000)
		<< "The selected school's two sources also multiply independently with an ANY source";
}

TEST_F(NewHorizonsMagicalDamageReductionTest, OlderSavedRulesWithoutHolyArmorMarkerKeepLegacySummedReduction)
{
	rulesetVersion = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	includeHolyArmorRosterRow = false;
	prepareBattle();
	ASSERT_NE(target, nullptr);
	target->addNewBonus(spellDamageReduction(50));
	target->addNewBonus(spellDamageReduction(20));

	const auto * selectedSpell = spell(SpellID::MAGIC_ARROW);
	ASSERT_NE(selectedSpell, nullptr);
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, selectedSpell);
	const auto mechanics = selectedSpell->battleMechanics(&event);
	EXPECT_FALSE(mechanics->usesNewHorizonsMultiplicativeMDR());
	const auto rawDamage = mechanics->getEffectValue();
	ASSERT_GT(rawDamage, 0);
	const auto expectedLegacyDamage = rawDamage * 30 / 100;
	EXPECT_EQ(mechanics->adjustEffectValue(target), expectedLegacyDamage);

	const auto healthBefore = target->getAvailableHealth();
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::MAGIC_ARROW, target));
	EXPECT_EQ(healthBefore - target->getAvailableHealth(), expectedLegacyDamage)
		<< "A v2 save without the Holy Armor roster marker retains the prior summed behavior";
}

TEST_F(NewHorizonsMagicalDamageReductionTest, CustomHolyArmorRosterIdentityOptsInWithoutCoreSpellEnum)
{
	prepareBattle();
	const auto * selectedSpell = spell(SpellID::MAGIC_ARROW);
	ASSERT_NE(selectedSpell, nullptr);
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, selectedSpell);
	EXPECT_TRUE(selectedSpell->battleMechanics(&event)->usesNewHorizonsMultiplicativeMDR())
		<< "The saved scoped key new-horizons:holyArmor is used directly; Holy Armor need not occupy a core SpellID";
}

TEST_F(NewHorizonsMagicalDamageReductionTest, CreatureOriginMagicalDamageUsesTheSavedReductionRules)
{
	prepareBattle();
	ASSERT_NE(target, nullptr);
	target->addNewBonus(spellDamageReduction(50));
	target->addNewBonus(spellDamageReduction(20));

	auto * creatureCaster = addStack(BattleSide::ATTACKER, creatureByName("core:imp"), BattleHex(4, 5), 1);
	ASSERT_NE(creatureCaster, nullptr);
	creatureCaster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELLCASTER, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW))));
	creatureCaster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID()));
	creatureCaster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::CREATURE_SPELL_POWER, BonusSource::OTHER, 20, BonusSourceID()));

	const auto * selectedSpell = spell(SpellID::MAGIC_ARROW);
	ASSERT_NE(selectedSpell, nullptr);
	spells::BattleCast event(battle(), creatureCaster, spells::Mode::CREATURE_ACTIVE, selectedSpell);
	const auto mechanics = selectedSpell->battleMechanics(&event);
	ASSERT_TRUE(mechanics->usesNewHorizonsMultiplicativeMDR());
	const auto rawDamage = mechanics->getEffectValue();
	ASSERT_GT(rawDamage, 0);
	const auto expectedDamage = rawDamage * 50 * 80 / 10000;
	EXPECT_EQ(mechanics->adjustEffectValue(target), expectedDamage);

	spells::Target destination{spells::Destination(target)};
	ASSERT_TRUE(mechanics->canBeCastAt(destination));
	const auto healthBefore = target->getAvailableHealth();
	event.cast(gameHandler->spellEnv.get(), destination);
	EXPECT_EQ(healthBefore - target->getAvailableHealth(), expectedDamage)
		<< "Creature casts use the same target reduction calculation as hero casts";
}

TEST_F(NewHorizonsMagicalDamageReductionTest, NonDamagingSpellEffectsAreNotReduced)
{
	prepareBattle();
	ASSERT_NE(target, nullptr);
	target->addNewBonus(spellDamageReduction(100));
	const auto * selectedSpell = spell(SpellID::SLOW);
	ASSERT_NE(selectedSpell, nullptr);
	ASSERT_FALSE(selectedSpell->isDamage());

	ASSERT_TRUE(castOn(attackerSideHero, SpellID::SLOW, target));
	EXPECT_TRUE(target->hasBonusOfType(BonusType::STACKS_SPEED))
		<< "Magical Damage Reduction protects damage only; Slow still applies";
}
