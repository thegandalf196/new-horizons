/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsHeroRulesFixture.h"
#include "../../lib/entities/hero/NewHorizonsChosenSpecialties.h"
#include "../../lib/entities/hero/HeroStartingProjection.h"
#include "../../lib/entities/hero/CHeroHandler.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../spells/HistoricalCrusadeRules.h"
#include "../../lib/GameSettings.h"
#include "../../lib/serializer/CMemorySerializer.h"

using namespace newHorizonsHeroes;
namespace
{
JsonNode configuredRules()
{
	return JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
}
const CHero & heroType(std::string_view key)
{
	for(const auto & hero : LIBRARY->heroh->objects)
		if(hero && hero->getJsonKey() == key)
			return *hero;
	throw std::runtime_error("Missing test hero");
}
}

TEST(NewHorizonsChosenSpecialties, CapturesTuningAndPreservesHistoricalAbsence)
{
	auto installed = configuredRules();
	const auto captured = resolveHeroRules(installed, HeroClassID(0));
	ASSERT_TRUE(chosenSpecialtyRules(captured));
	EXPECT_EQ(chosenSpecialtyRules(captured)->inteusDurationBonus, 1);
	EXPECT_EQ(chosenSpecialtyRules(captured)->loynisManaDiscountPercent, 20);
	installed["chosenSpecialties"]["inteusDurationBonus"].Integer() = 7;
	EXPECT_EQ(chosenSpecialtyRules(captured)->inteusDurationBonus, 1);
	auto historical = captured;
	historical.Struct().erase("chosenSpecialties");
	const auto before = historical;
	EXPECT_FALSE(chosenSpecialtyRules(historical));
	EXPECT_NO_THROW(validateResolvedHeroRules(historical));
	EXPECT_NO_THROW(validateChosenSpecialtiesSerialization(historical, false));
	EXPECT_EQ(historical, before);
}

TEST(NewHorizonsChosenSpecialties, RejectsUnknownMalformedAndUnsupportedPolicy)
{
	auto rules = configuredRules();
	EXPECT_THROW(validateChosenSpecialtiesSerialization(rules, false), std::runtime_error);
	rules["chosenSpecialties"]["version"].Integer() = 2;
	EXPECT_THROW(validateHeroRules(rules, false), std::runtime_error);
	rules = configuredRules();
	rules["chosenSpecialties"]["loynisManaDiscountPercent"].Integer() = 101;
	EXPECT_THROW(validateHeroRules(rules, false), std::runtime_error);
	rules = configuredRules();
	rules["chosenSpecialties"]["extra"].Integer() = 1;
	EXPECT_THROW(validateHeroRules(rules, false), std::runtime_error);
}

TEST(NewHorizonsChosenSpecialties, CurrentStartingBooksReplaceOriginalInscriptionOnly)
{
	const auto rules = configuredRules();
	const JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
	const SpellID chosen(SpellID::decode("new-horizons:theChosen"));
	ASSERT_TRUE(chosen.hasValue());
	EXPECT_EQ(startingHeroSpellReplacement(heroType("core:inteus"), rules, magic, SpellID::BLOODLUST), chosen);
	EXPECT_EQ(startingHeroSpellReplacement(heroType("core:loynis"), rules, magic, SpellID::PRAYER), chosen);
	EXPECT_FALSE(chosenStartingSpellReplacement(rules, magic, "core:halon", SpellID::STONE_SKIN));
	EXPECT_FALSE(chosenStartingSpellReplacement(rules, magic, "core:inteus", SpellID::PRAYER));
}

TEST(NewHorizonsChosenSpecialties, HistoricalStartingPolicyStillSelectsCrusade)
{
	auto rules = configuredRules();
	rules.Struct().erase("chosenSpecialties");
	const auto magic = historicalCrusadeRules(JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	const SpellID crusade(SpellID::decode("new-horizons:crusade"));
	EXPECT_EQ(startingHeroSpellReplacement(heroType("core:inteus"), rules, magic, SpellID::BLOODLUST), crusade);
	EXPECT_EQ(startingHeroSpellReplacement(heroType("core:loynis"), rules, magic, SpellID::PRAYER), crusade);
}

TEST(NewHorizonsChosenSpecialties, SeparateRidersNeverScaleChosenAbilitiesOrLegacyCrusade)
{
	const auto rules = configuredRules();
	const SpellID chosen(SpellID::decode("new-horizons:theChosen"));
	const SpellID crusade(SpellID::decode("new-horizons:crusade"));
	EXPECT_EQ(chosenSpecialtyDurationBonus(rules, "core:inteus", chosen), 1);
	EXPECT_EQ(chosenSpecialtyDurationBonus(rules, "core:loynis", chosen), 0);
	EXPECT_EQ(chosenSpecialtyManaDiscount(rules, "core:loynis", chosen), 20);
	EXPECT_EQ(chosenSpecialtyManaDiscount(rules, "core:inteus", chosen), 0);
	EXPECT_EQ(chosenSpecialtyDurationBonus(rules, "core:inteus", crusade), 0);
	EXPECT_EQ(chosenSpecialtyManaDiscount(rules, "core:loynis", crusade), 0);
	const auto ordinary = nonDamageSpellSpecialtyRules(rules);
	ASSERT_TRUE(ordinary);
	EXPECT_EQ(std::ranges::find(ordinary->spells, chosen), ordinary->spells.end());
}

TEST(NewHorizonsChosenSpecialties, ManaDiscountRoundsUpAfterWisdomAndKeepsMinimumOne)
{
	EXPECT_EQ(chosenSpecialtyAdjustedCost(11, 20), 9);
	EXPECT_EQ(chosenSpecialtyAdjustedCost(10, 20), 8);
	EXPECT_EQ(chosenSpecialtyAdjustedCost(1, 20), 1);
	EXPECT_EQ(chosenSpecialtyAdjustedCost(10, 100), 1);
	EXPECT_EQ(chosenSpecialtyAdjustedCost(0, 0), 0);
	EXPECT_EQ(chosenSpecialtyAdjustedCost(11, 0), 11);
	EXPECT_EQ(chosenSpecialtyAdjustedCost(newHorizonsMagic::wisdomAdjustedCost(11, 1, 1), 20), 8);
}

TEST(NewHorizonsChosenSpecialties, OldSettingsWriterRejectsBeforePrefixAndCurrentRoundtripKeepsPolicy)
{
	JsonNode captured;
	captured["chosenSpecialties"] = configuredRules()["chosenSpecialties"];
	GameSettings settings;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, captured);
	CMemorySerializer oldWire;
	oldWire.oser.version = ESerializationVersion::NEW_HORIZONS_REMAINING_SPECIALTY_RIDERS;
	EXPECT_THROW(settings.serialize(oldWire.oser), std::runtime_error);
	EXPECT_TRUE(oldWire.extractBuffer().empty());
	CMemorySerializer currentWire;
	ASSERT_NO_THROW(settings.serialize(currentWire.oser));
	CMemorySerializer reader(currentWire.extractBuffer());
	GameSettings restored;
	ASSERT_NO_THROW(restored.serialize(reader.iser));
	EXPECT_EQ(restored.getValue(EGameSettings::HEROES_NEW_HORIZONS), captured);
}
