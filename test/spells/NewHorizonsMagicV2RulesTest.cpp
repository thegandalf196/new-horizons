/*
 * NewHorizonsMagicV2RulesTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <stdexcept>

namespace
{
constexpr auto arrowKey = "core:magicArrow";
constexpr auto quicksandKey = "core:quicksand";
JsonNode originalRules()
{
	return JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
}

JsonNode legacyRules()
{
	auto rules = originalRules();
	rules["rulesetVersion"].Integer() = newHorizonsMagic::RULESET_VERSION;
	rules.Struct().erase("spellPoints");
	rules.Struct().erase("mageGuildGeneration");
	rules.Struct().erase("physicalDamageReductionCapPercent");
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	rules.Struct().erase("warcasting");
	for(auto & [factionId, faction] : rules["factions"].Struct())
	{
		(void)factionId;
		faction["major"] = faction["preferredA"];
		faction["minor"] = faction["preferredB"];
		faction.Struct().erase("preferredA");
		faction.Struct().erase("preferredB");
	}
	auto & savedSpells = rules["spells"].Struct();
	for(auto it = savedSpells.begin(); it != savedSpells.end();)
	{
		if(it->first.starts_with("new-horizons:"))
		{
			it = savedSpells.erase(it);
			continue;
		}
		auto & spell = it->second;
		spell.Struct().erase("active");
		spell.Struct().erase("directDamage");
		spell.Struct().erase("cureAfflictions");
		spell.Struct().erase("selectedPlacement");
		++it;
	}
	return rules;
}

JsonNode formulaRules()
{
	auto rules = originalRules();
	rules["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	rules["spells"][quicksandKey].Struct().erase("selectedPlacement");
	// Existing registered identity for rules-only tests; this does not alter the
	// installed spell or activate the proposed new Magic Missile definition.
	rules["spells"][arrowKey]["directDamage"]["base"].Integer() = 20;
	rules["spells"][arrowKey]["directDamage"]["powerCoefficient"].Integer() = 20;
	return rules;
}
}

TEST(NewHorizonsMagicV2RulesTest, ActualV1AndV2DecodeWithoutChangingExistingSchoolsLevelsOrCosts)
{
	const auto old = legacyRules();
	const auto current = formulaRules();
	EXPECT_FALSE(old.Struct().contains("mageGuildGeneration"));
	for(const auto & [factionId, faction] : old["factions"].Struct())
	{
		(void)factionId;
		EXPECT_TRUE(faction.Struct().contains("major"));
		EXPECT_TRUE(faction.Struct().contains("minor"));
		EXPECT_FALSE(faction.Struct().contains("preferredA"));
		EXPECT_FALSE(faction.Struct().contains("preferredB"));
	}
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(old));
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(current));
	const SpellID arrow(SpellID::decode(arrowKey));
	ASSERT_NE(arrow, SpellID::NONE);
	EXPECT_EQ(newHorizonsMagic::activeSchools(current), newHorizonsMagic::activeSchools(old));
	EXPECT_EQ(newHorizonsMagic::spellSchools(current, arrow), newHorizonsMagic::spellSchools(old, arrow));
	EXPECT_EQ(newHorizonsMagic::spellLevel(current, arrow), newHorizonsMagic::spellLevel(old, arrow));
	for(int rank = 0; rank <= 3; ++rank)
		EXPECT_EQ(newHorizonsMagic::spellCost(current, arrow, rank), newHorizonsMagic::spellCost(old, arrow, rank));
}

TEST(NewHorizonsMagicV2RulesTest, V1StillRejectsFormulaAndUnsupportedEnvelopeFails)
{
	auto rules = formulaRules();
	rules["rulesetVersion"].Integer() = 1;
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	EXPECT_THROW(newHorizonsMagic::spellDirectDamage(rules, arrowKey), std::runtime_error);
	rules = formulaRules();
	rules["rulesetVersion"].Integer() = 4;
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	rules = formulaRules();
	rules["schemaVersion"].Integer() = 2;
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	rules = formulaRules();
	rules["rulesetVersion"].Float() = 2.0;
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	EXPECT_THROW(newHorizonsMagic::spellDirectDamage(rules, arrowKey), std::runtime_error);
}

TEST(NewHorizonsMagicV2RulesTest, V3SnapshotsSchoolFactorsAndV2KeepsOneHundredPercent)
{
	const auto current = originalRules();
	EXPECT_EQ(current["rulesetVersion"].Integer(), newHorizonsMagic::CURRENT_RULESET_VERSION);
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(current));
	const std::array<int, 4> expected{100, 115, 130, 145};
	for(int rank = 0; rank < static_cast<int>(expected.size()); ++rank)
		EXPECT_EQ(newHorizonsMagic::schoolRankPowerCoefficientPercent(current, rank), expected[rank]);
	EXPECT_EQ(newHorizonsMagic::schoolRankPowerCoefficientPercent(formulaRules(), MasteryLevel::EXPERT), 100)
		<< "Older v2 saves retain the unranked coefficient even when the installed module is newer";

	auto missing = current;
	missing.Struct().erase("schoolRankPowerCoefficientPercent");
	EXPECT_THROW(newHorizonsMagic::validateRules(missing), std::runtime_error);
	auto malformed = current;
	malformed["schoolRankPowerCoefficientPercent"].Vector()[2].Integer() = 135;
	EXPECT_THROW(newHorizonsMagic::validateRules(malformed), std::runtime_error);
	malformed = current;
	malformed["schoolRankPowerCoefficientPercent"].Vector()[3].Float() = 145.0;
	EXPECT_THROW(newHorizonsMagic::validateRules(malformed), std::runtime_error);

	auto v2WithV3Field = formulaRules();
	v2WithV3Field["schoolRankPowerCoefficientPercent"] = current["schoolRankPowerCoefficientPercent"];
	EXPECT_THROW(newHorizonsMagic::validateRules(v2WithV3Field), std::runtime_error);
	v2WithV3Field = formulaRules();
	v2WithV3Field["spellcraftEfficiencyPercent"] = current["spellcraftEfficiencyPercent"];
	EXPECT_THROW(newHorizonsMagic::validateRules(v2WithV3Field), std::runtime_error);
	auto v1WithSpellcraftField = legacyRules();
	v1WithSpellcraftField["spellcraftEfficiencyPercent"] = current["spellcraftEfficiencyPercent"];
	EXPECT_THROW(newHorizonsMagic::validateRules(v1WithSpellcraftField), std::runtime_error);
}

TEST(NewHorizonsMagicV2RulesTest, OptionalSavedSpellcraftEfficiencyKeepsOlderV3AtOneHundredPercent)
{
	const auto current = originalRules();
	const std::array<int, 4> expected{100, 110, 120, 130};
	ASSERT_EQ(current["spellcraftEfficiencyPercent"].Vector().size(), expected.size());
	for(int rank = 0; rank < static_cast<int>(expected.size()); ++rank)
		EXPECT_EQ(newHorizonsMagic::spellcraftEfficiencyPercent(current, rank), expected[static_cast<size_t>(rank)]);

	auto oldV3 = current;
	oldV3.Struct().erase("spellcraftEfficiencyPercent");
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(oldV3));
	for(int rank = 0; rank < static_cast<int>(expected.size()); ++rank)
		EXPECT_EQ(newHorizonsMagic::spellcraftEfficiencyPercent(oldV3, rank), 100);
	EXPECT_EQ(newHorizonsMagic::spellcraftEfficiencyPercent(formulaRules(), MasteryLevel::EXPERT), 100)
		<< "V2 saves cannot acquire Spellcraft efficiency from installed rules";

	auto malformed = current;
	malformed["spellcraftEfficiencyPercent"].Vector().pop_back();
	EXPECT_THROW(newHorizonsMagic::validateRules(malformed), std::runtime_error);
	malformed = current;
	malformed["spellcraftEfficiencyPercent"].Vector()[2].Integer() = 125;
	EXPECT_THROW(newHorizonsMagic::validateRules(malformed), std::runtime_error);
	malformed = current;
	malformed["spellcraftEfficiencyPercent"].Vector()[3].Float() = 130.0;
	EXPECT_THROW(newHorizonsMagic::validateRules(malformed), std::runtime_error);
}

TEST(NewHorizonsMagicV2RulesTest, SchoolAndSpellcraftFactorsComposeAsExactBasisPoints)
{
	const auto current = originalRules();
	const auto spellcraft = SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft"));
	ASSERT_NE(spellcraft, SecondarySkill::NONE);
	const auto arrow = SpellID(SpellID::decode(arrowKey));
	ASSERT_NE(arrow, SpellID::NONE);
	const auto arrowSchools = newHorizonsMagic::spellSchoolSkills(current, arrow);
	ASSERT_EQ(arrowSchools.size(), 1u);

	CGHeroInstance hero(nullptr);
	const std::array<int, 4> schoolFactors{100, 115, 130, 145};
	const std::array<int, 4> spellcraftFactors{100, 110, 120, 130};
	for(int schoolRank = 0; schoolRank < 4; ++schoolRank)
	{
		for(int spellcraftRank = 0; spellcraftRank < 4; ++spellcraftRank)
		{
			hero.secSkills.clear();
			hero.secSkills.emplace_back(arrowSchools.front(), schoolRank);
			hero.secSkills.emplace_back(spellcraft, spellcraftRank);
			EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(current, &hero, arrow),
				schoolFactors[static_cast<size_t>(schoolRank)] * spellcraftFactors[static_cast<size_t>(spellcraftRank)])
				<< "school rank " << schoolRank << ", Spellcraft rank " << spellcraftRank;
		}
	}
	EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(current, nullptr, arrow),
		newHorizonsMagic::SPELL_POWER_COEFFICIENT_BASIS_POINTS);
	hero.secSkills.clear();
	hero.secSkills.emplace_back(arrowSchools.front(), MasteryLevel::EXPERT);
	EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(current, &hero, arrow), 14'500)
		<< "A School Skill rank does not stand in for the separate Spellcraft Skill";
	hero.secSkills.clear();
	hero.secSkills.emplace_back(spellcraft, MasteryLevel::BASIC);
	EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(current, &hero, arrow), 11'000)
		<< "Spellcraft reads the actual registered Skill rank independently of School rank";

	const auto multiSchoolSpell = SpellID(SpellID::decode("core:airElemental"));
	ASSERT_NE(multiSchoolSpell, SpellID::NONE);
	const auto multiSchoolSkills = newHorizonsMagic::spellSchoolSkills(current, multiSchoolSpell);
	ASSERT_EQ(multiSchoolSkills.size(), 2u);
	hero.secSkills.clear();
	hero.secSkills.emplace_back(multiSchoolSkills[0], MasteryLevel::EXPERT);
	hero.secSkills.emplace_back(multiSchoolSkills[1], MasteryLevel::BASIC);
	hero.secSkills.emplace_back(spellcraft, MasteryLevel::EXPERT);
	EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(current, &hero, multiSchoolSpell), 18'850)
		<< "The highest school rank is used once, then multiplied by Spellcraft once";

	auto oldV3 = current;
	oldV3.Struct().erase("spellcraftEfficiencyPercent");
	hero.secSkills.clear();
	hero.secSkills.emplace_back(arrowSchools.front(), MasteryLevel::BASIC);
	hero.secSkills.emplace_back(spellcraft, MasteryLevel::EXPERT);
	EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(oldV3, &hero, arrow), 11'500)
		<< "An older v3 snapshot applies only its saved School factor";
}

TEST(NewHorizonsMagicV2RulesTest, QuicksandPatchCountUsesSavedSchoolAndSpellcraftOnPowerTermOnly)
{
	const auto current = originalRules();
	const auto quicksand = SpellID(SpellID::decode(quicksandKey));
	ASSERT_NE(quicksand, SpellID::NONE);
	const auto schoolSkills = newHorizonsMagic::spellSchoolSkills(current, quicksand);
	ASSERT_EQ(schoolSkills.size(), 1u);
	const auto spellcraft = SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft"));
	ASSERT_NE(spellcraft, SecondarySkill::NONE);

	CGHeroInstance hero(nullptr);
	const std::array<int, 7> power{0, 59, 60, 119, 120, 180, 1000};
	const std::array<int, 7> expected{2, 2, 3, 3, 4, 5, 5};
	for(size_t index = 0; index < power.size(); ++index)
	{
		auto count = newHorizonsMagic::quicksandPatchCount(current, &hero, quicksand, power[index]);
		ASSERT_TRUE(count);
		EXPECT_EQ(*count, expected[index]) << "Spell Power " << power[index];
	}

	hero.secSkills.emplace_back(schoolSkills.front(), MasteryLevel::BASIC);
	auto rankedCount = newHorizonsMagic::quicksandPatchCount(current, &hero, quicksand, 59);
	ASSERT_TRUE(rankedCount);
	EXPECT_EQ(*rankedCount, 3) << "Basic Nature scales the Spell Power term past the first threshold";
	auto zeroPowerCount = newHorizonsMagic::quicksandPatchCount(current, &hero, quicksand, 0);
	ASSERT_TRUE(zeroPowerCount);
	EXPECT_EQ(*zeroPowerCount, newHorizonsMagic::QUICKSAND_BASE_PATCH_COUNT_V3)
		<< "School rank does not scale the fixed base patches";

	hero.secSkills.clear();
	hero.secSkills.emplace_back(spellcraft, MasteryLevel::EXPERT);
	const auto spellcraftCount = newHorizonsMagic::quicksandPatchCount(current, &hero, quicksand, 48);
	ASSERT_TRUE(spellcraftCount);
	EXPECT_EQ(*spellcraftCount, 3) << "Expert Spellcraft scales only the Spell Power term";

	hero.secSkills.clear();
	const auto unmodifiedCount = newHorizonsMagic::quicksandPatchCount(current, &hero, quicksand, 48);
	ASSERT_TRUE(unmodifiedCount);
	EXPECT_EQ(*unmodifiedCount, 2) << "48 Spell Power is below the first patch threshold without modifiers";
	const auto empoweredCount = newHorizonsMagic::quicksandPatchCount(current, &hero, quicksand, 48,
		1, 0, 25);
	ASSERT_TRUE(empoweredCount);
	EXPECT_EQ(*empoweredCount, 3) << "Empower alone can cross the first patch threshold";
	const auto warcastCount = newHorizonsMagic::quicksandPatchCount(current, &hero, quicksand, 48,
		1, 25, 0);
	ASSERT_TRUE(warcastCount);
	EXPECT_EQ(*warcastCount, 3) << "Warcasting alone can cross the first patch threshold";

	const auto v1 = legacyRules();
	const auto v2 = formulaRules();
	EXPECT_FALSE(newHorizonsMagic::quicksandPatchCount(v1, &hero, quicksand, 180));
	EXPECT_FALSE(newHorizonsMagic::quicksandPatchCount(v2, &hero, quicksand, 180));
	EXPECT_FALSE(newHorizonsMagic::quicksandPatchCount(current, &hero, SpellID(SpellID::HASTE), 180));
}

TEST(NewHorizonsMagicV2RulesTest, QuicksandSelectedPlacementIsStrictSavedV3OptIn)
{
	const auto quicksand = SpellID(SpellID::decode(quicksandKey));
	ASSERT_NE(quicksand, SpellID::NONE);
	auto current = originalRules();
	EXPECT_TRUE(newHorizonsMagic::quicksandSelectedPlacementEnabled(current, quicksand));
	EXPECT_FALSE(newHorizonsMagic::quicksandSelectedPlacementEnabled(current, SpellID(SpellID::HASTE)));

	auto markerlessV3 = current;
	markerlessV3["spells"][quicksandKey].Struct().erase("selectedPlacement");
	EXPECT_FALSE(newHorizonsMagic::quicksandSelectedPlacementEnabled(markerlessV3, quicksand));
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(markerlessV3));

	EXPECT_FALSE(newHorizonsMagic::quicksandSelectedPlacementEnabled(formulaRules(), quicksand));
	EXPECT_FALSE(newHorizonsMagic::quicksandSelectedPlacementEnabled(legacyRules(), quicksand));

	auto malformed = current;
	malformed["spells"][quicksandKey]["selectedPlacement"].String() = "true";
	EXPECT_THROW(newHorizonsMagic::validateRules(malformed), std::runtime_error);
	malformed = current;
	malformed["spells"]["core:haste"]["selectedPlacement"].Bool() = true;
	EXPECT_THROW(newHorizonsMagic::validateRules(malformed), std::runtime_error);
	malformed = formulaRules();
	malformed["spells"][quicksandKey]["selectedPlacement"].Bool() = true;
	EXPECT_THROW(newHorizonsMagic::validateRules(malformed), std::runtime_error);
}

TEST(NewHorizonsMagicV2RulesTest, AdventureSpellsAndCreatureAbilitiesExcludeBothRankFactors)
{
	const auto current = originalRules();
	const auto spellcraft = SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft"));
	ASSERT_NE(spellcraft, SecondarySkill::NONE);
	CGHeroInstance hero(nullptr);
	hero.secSkills.emplace_back(spellcraft, MasteryLevel::EXPERT);

	const auto adventureSpell = SpellID(SpellID::decode("core:summonBoat"));
	ASSERT_NE(adventureSpell, SpellID::NONE);
	EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(current, &hero, adventureSpell), 10'000);

	const auto creatureAbility = SpellID(SpellID::decode("core:stoneGaze"));
	ASSERT_NE(creatureAbility, SpellID::NONE);
	ASSERT_TRUE(creatureAbility.toSpell());
	EXPECT_FALSE(creatureAbility.toSpell()->isCommonHeroSpell());
	EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(current, &hero, creatureAbility), 10'000);
}

TEST(NewHorizonsMagicV2RulesTest, BasisPointSpellPowerTermsKeepFractionalFactorsUntilFinalFloor)
{
	EXPECT_EQ(newHorizonsMagic::regenerationRateMillionthsBasisPoints(1, 10'000, false),
		newHorizonsMagic::regenerationRateMillionths(1, 100, false));
	EXPECT_EQ(newHorizonsMagic::regenerationRateMillionthsBasisPoints(1, 12'650, false), 251'897)
		<< "The combined 126.5% factor is applied once before the final rate floor";
	EXPECT_EQ(newHorizonsMagic::regenerationRateMillionthsBasisPoints(1, 12'650, false, 20), 252'277)
		<< "Warcasting composes before the same final integer floor";
	EXPECT_EQ(newHorizonsMagic::poisonBaseDamageBasisPoints(3, 10'000),
		newHorizonsMagic::poisonBaseDamage(3, 100));
	EXPECT_EQ(newHorizonsMagic::poisonBaseDamageBasisPoints(3, 12'650), 21)
		<< "Poison preserves the combined fractional factor until final integer damage";
}

TEST(NewHorizonsMagicV2RulesTest, ExpertMassRangeOverrideIsSavedV3AndSpellSpecific)
{
	const auto v1 = legacyRules();
	const auto v2 = formulaRules();
	const auto v3 = originalRules();
	const std::array affectedSpells{
		SpellID(SpellID::CURE), SpellID(SpellID::BLESS), SpellID(SpellID::CURSE),
		SpellID(SpellID::SLOW), SpellID(SpellID::DISPEL), SpellID(SpellID::SHIELD),
		SpellID(SpellID::AIR_SHIELD), SpellID(SpellID::PROTECTION_FROM_AIR),
		SpellID(SpellID::PROTECTION_FROM_FIRE), SpellID(SpellID::PROTECTION_FROM_WATER),
		SpellID(SpellID::PROTECTION_FROM_EARTH), SpellID(SpellID::BLOODLUST),
		SpellID(SpellID::PRECISION), SpellID(SpellID::WEAKNESS), SpellID(SpellID::STONE_SKIN),
		SpellID(SpellID::PRAYER), SpellID(SpellID::MIRTH), SpellID(SpellID::SORROW),
		SpellID(SpellID::FORTUNE), SpellID(SpellID::MISFORTUNE), SpellID(SpellID::HASTE),
		SpellID(SpellID::COUNTERSTRIKE), SpellID(SpellID::FORGETFULNESS),
	};
	for(const auto spell : affectedSpells)
	{
		SCOPED_TRACE(spell.getNum());
		EXPECT_FALSE(newHorizonsMagic::expertRangeIsSingleTarget(v1, spell));
		EXPECT_FALSE(newHorizonsMagic::expertRangeIsSingleTarget(v2, spell));
		EXPECT_TRUE(newHorizonsMagic::expertRangeIsSingleTarget(v3, spell));
	}
	EXPECT_FALSE(newHorizonsMagic::expertRangeIsSingleTarget(v3, SpellID(SpellID::MAGIC_ARROW)));
	EXPECT_FALSE(newHorizonsMagic::expertRangeIsSingleTarget(v3, SpellID(SpellID::BERSERK)));
	EXPECT_FALSE(newHorizonsMagic::expertRangeIsSingleTarget(v3, SpellID(SpellID::CHAIN_LIGHTNING)));
	EXPECT_FALSE(newHorizonsMagic::expertRangeIsSingleTarget(JsonNode(), SpellID(SpellID::BLESS)));
}

TEST(NewHorizonsMagicV2RulesTest, V2RejectsMalformedFormulaBeforeUse)
{
	auto rules = formulaRules();
	rules["spells"][arrowKey]["directDamage"] = JsonNode();
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	rules = formulaRules();
	rules["spells"][arrowKey]["directDamage"]["base"].Float() = 20.0;
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	rules = formulaRules();
	rules["spells"][arrowKey]["directDamage"]["powerCoefficient"].Integer() = -1;
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	rules = formulaRules();
	rules["spells"][arrowKey]["directDamage"]["other"].Integer() = 1;
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
}

TEST(NewHorizonsMagicV2RulesTest, ActiveSpellMarkerMustBeBooleanAndOldRowsStayValid)
{
	auto rules = formulaRules();
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(rules));
	rules["spells"]["core:clone"].Struct().erase("active");
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(rules));
	rules["spells"]["core:clone"]["active"].String() = "false";
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
}

TEST(NewHorizonsMagicV2RulesTest, WarcastingOptInIsOptionalAndStrictlyBoolean)
{
	auto rules = formulaRules();
	rules.Struct().erase("warcasting");
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(rules));
	rules["warcasting"].Bool() = false;
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(rules));
	rules["warcasting"].Bool() = true;
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(rules));
	rules["warcasting"].String() = "true";
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	rules["warcasting"].Integer() = 1;
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	rules["warcasting"] = JsonNode();
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
}

TEST(NewHorizonsMagicV2RulesTest, SpellPointOptInIsV2OnlyAndUsesSavedCapacityPercent)
{
	auto rules = originalRules();
	ASSERT_TRUE(rules["spellPoints"].isStruct());
	EXPECT_TRUE(newHorizonsMagic::spellPointRulesActive(rules));
	EXPECT_EQ(newHorizonsMagic::spellPointsIntelligenceMaximumPercent(rules), 130);

	rules["spellPoints"]["intelligenceMaximumPercent"].Integer() = 175;
	EXPECT_EQ(newHorizonsMagic::spellPointsIntelligenceMaximumPercent(rules), 175);

	auto absent = rules;
	absent.Struct().erase("spellPoints");
	EXPECT_FALSE(newHorizonsMagic::spellPointRulesActive(absent));
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(absent));

	auto legacy = rules;
	legacy["rulesetVersion"].Integer() = newHorizonsMagic::RULESET_VERSION;
	EXPECT_FALSE(newHorizonsMagic::spellPointRulesActive(legacy));
	EXPECT_THROW(newHorizonsMagic::validateRules(legacy), std::runtime_error)
		<< "The opt-in must not silently change an existing v1 snapshot";

	auto malformed = rules;
	malformed["spellPoints"]["intelligenceMaximumPercent"].Float() = 175.0;
	EXPECT_FALSE(newHorizonsMagic::spellPointRulesActive(malformed));
	EXPECT_THROW(newHorizonsMagic::validateRules(malformed), std::runtime_error);
	malformed = rules;
	malformed["spellPoints"]["intelligenceMaximumPercent"].Integer() = 99;
	EXPECT_FALSE(newHorizonsMagic::spellPointRulesActive(malformed));
	EXPECT_THROW(newHorizonsMagic::validateRules(malformed), std::runtime_error);
}

TEST(NewHorizonsMagicV2RulesTest, AbsentSnapshotRowAndOptionalFormulaNeverUseInstalledDamage)
{
	EXPECT_FALSE(newHorizonsMagic::spellDirectDamage(JsonNode(), arrowKey));
	EXPECT_FALSE(newHorizonsMagic::spellDirectDamage(JsonNode(JsonMap{}), arrowKey));
	EXPECT_TRUE(newHorizonsMagic::spellDirectDamage(originalRules(), arrowKey));
	auto rules = formulaRules();
	EXPECT_FALSE(newHorizonsMagic::spellDirectDamage(rules, "new-horizons:magicMissile"));
	constexpr auto optionalFormulaKey = "core:armageddon";
	ASSERT_TRUE(newHorizonsMagic::spellDirectDamage(rules, optionalFormulaKey));
	rules["spells"][optionalFormulaKey].Struct().erase("directDamage");
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(rules));
	EXPECT_FALSE(newHorizonsMagic::spellDirectDamage(rules, optionalFormulaKey));
	EXPECT_FALSE(newHorizonsMagic::directDamageValue(rules, optionalFormulaKey, 24, 10));
	EXPECT_THROW(newHorizonsMagic::spellDirectDamage(rules, "magicArrow"), std::runtime_error);
}

TEST(NewHorizonsMagicV2RulesTest, FullValidationRetainsEntityResolutionAndRequiredCommonCoverage)
{
	auto rules = formulaRules();
	rules["spells"]["core:nonexistentFormulaSpell"] = rules["spells"][arrowKey];
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	rules = formulaRules();
	rules["spells"].Struct().erase(arrowKey);
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
}

TEST(NewHorizonsMagicV2RulesTest, SavedRowAccessUsesWidePrimitiveAndReturnsIndependentValue)
{
	auto rules = formulaRules();
	const auto captured = newHorizonsMagic::spellDirectDamage(rules, arrowKey);
	ASSERT_TRUE(captured);
	EXPECT_EQ(newHorizonsMagic::directDamageValue(rules, arrowKey, 5, 10), 30);
	EXPECT_EQ(newHorizonsMagic::directDamageValue(rules, arrowKey, 24, 10), 68);
	EXPECT_EQ(newHorizonsMagic::directDamageValue(rules, arrowKey, 96, 10), 212);
	EXPECT_EQ(newHorizonsMagic::directDamageValue(rules, arrowKey, 5, 1), 120);
	EXPECT_THROW(newHorizonsMagic::directDamageValue(rules, arrowKey, 5, 0), std::runtime_error);
	rules["spells"][arrowKey]["directDamage"]["base"].Integer() = 100;
	EXPECT_EQ(captured->evaluate(5, 10), 30);
	EXPECT_EQ(newHorizonsMagic::directDamageValue(rules, arrowKey, 5, 10), 110);
}

TEST(NewHorizonsMagicV2RulesTest, JsonSnapshotRoundTripRetainsFormulaNotLaterSourceValues)
{
	auto source = formulaRules();
	CMemorySerializer wire;
	wire.oser & source;
	source["spells"][arrowKey]["directDamage"]["base"].Integer() = 999;
	JsonNode restored;
	wire.iser & restored;
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(restored));
	EXPECT_EQ(newHorizonsMagic::directDamageValue(restored, arrowKey, 24, 10), 68);
	// JsonNode round-trip only, not actual CGameState/BattleStart entrypoints.
}
