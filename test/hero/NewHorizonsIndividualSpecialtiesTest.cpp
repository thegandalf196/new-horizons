/*
 * NewHorizonsIndividualSpecialtiesTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsHeroRulesFixture.h"
#include "../../lib/entities/hero/NewHorizonsNecromancy.h"
#include "../../lib/GameSettings.h"
#include "../../lib/json/JsonUtils.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <limits>

using namespace newHorizonsHeroes;
namespace
{
JsonNode riderRules()
{
	auto result = testHeroRules();
	auto & row = result["individualSpecialties"];
	row["version"].Integer() = 1;
	row["graveReaper"]["percentagePointsPerLevel"].Integer() = 1;
	row["graveReaper"]["maximumPercentagePoints"].Integer() = 20;
	row["warlocksFormula"]["percentPerLevel"].Integer() = 5;
	row["warlocksFormula"]["maximumPercent"].Integer() = 100;
	return result;
}
}

TEST(NewHorizonsIndividualSpecialties, CapturesRulesWithoutFillingHistoricalAbsence)
{
	auto configured = riderRules();
	ASSERT_NO_THROW(validateHeroRules(configured, true));
	const auto captured = resolveHeroRules(configured, HeroClassID(0));
	const auto policy = individualSpecialtyRules(captured);
	ASSERT_TRUE(policy);
	EXPECT_EQ(policy->graveReaper.percentagePointsPerLevel, 1);
	EXPECT_EQ(policy->graveReaper.maximumPercentagePoints, 20);
	EXPECT_EQ(policy->warlocksFormula.percentPerLevel, 5);
	EXPECT_EQ(policy->warlocksFormula.maximumPercent, 100);
	configured["individualSpecialties"]["graveReaper"]["percentagePointsPerLevel"].Integer() = 9;
	EXPECT_EQ(individualSpecialtyRules(captured)->graveReaper.percentagePointsPerLevel, 1);
	auto old = captured;
	old.Struct().erase("individualSpecialties");
	const auto before = old;
	EXPECT_FALSE(individualSpecialtyRules(old));
	EXPECT_NO_THROW(validateResolvedHeroRules(old));
	EXPECT_NO_THROW(validateIndividualSpecialtiesSerialization(old, false, false));
	EXPECT_EQ(old, before);
}

TEST(NewHorizonsIndividualSpecialties, RejectsMalformedAndUnsupportedCaptures)
{
	const auto valid = resolveHeroRules(riderRules(), HeroClassID(0));
	EXPECT_THROW(validateIndividualSpecialtiesSerialization(valid, true, false), std::runtime_error);
	std::vector<JsonNode> malformed(7, valid);
	malformed[0]["individualSpecialties"] = JsonNode();
	malformed[1]["individualSpecialties"]["version"].Integer() = 2;
	malformed[2]["individualSpecialties"].Struct().erase("graveReaper");
	malformed[3]["individualSpecialties"]["graveReaper"]["percentagePointsPerLevel"].Integer() = -1;
	malformed[4]["individualSpecialties"]["warlocksFormula"]["percentPerLevel"].Float() = 0.5;
	malformed[5]["individualSpecialties"]["warlocksFormula"]["maximumPercent"].Integer() = 1001;
	malformed[6]["individualSpecialties"]["unexpected"].Bool() = true;
	for(const auto & rules : malformed)
	{
		EXPECT_THROW(validateResolvedHeroRules(rules), std::runtime_error);
		EXPECT_THROW(individualSpecialtyRules(rules), std::runtime_error);
	}
}

TEST(NewHorizonsIndividualSpecialties, LevelCapsAndSpellcraftContributionKeepFractionalPrecision)
{
	EXPECT_EQ(cappedSpecialtyContribution(-1, 1, 20), 0);
	EXPECT_EQ(cappedSpecialtyContribution(0, 1, 20), 0);
	EXPECT_EQ(cappedSpecialtyContribution(1, 1, 20), 1);
	EXPECT_EQ(cappedSpecialtyContribution(20, 1, 20), 20);
	EXPECT_EQ(cappedSpecialtyContribution(std::numeric_limits<int>::max(), 1000, 20), 20);
	EXPECT_EQ(cappedSpecialtyContribution(2, 3, 5), 5);
	EXPECT_EQ(strengthenedSpellcraftBasisPoints(100, 100), 10000);
	EXPECT_EQ(strengthenedSpellcraftBasisPoints(110, 5), 11050);
	EXPECT_EQ(strengthenedSpellcraftBasisPoints(120, 50), 13000);
	EXPECT_EQ(strengthenedSpellcraftBasisPoints(130, 100), 16000);
	EXPECT_EQ(strengthenedSpellcraftBasisPoints(130, 0), 13000);
}

TEST(NewHorizonsIndividualSpecialties, GraveReaperAddsBeforeFloorsWithoutChangingIndependentPools)
{
	const auto resolve = [](int rank, bool bone, int amplifier, int specialty)
	{
		return newHorizonsNecromancy::resolve(rank, 99, 0, bone, false, false,
			true, true, 0, 0, 0, false, true, CreatureID::NONE,
			{100, 0, 0}, {100, 0, 0}, false, false, true, amplifier, specialty);
	};
	EXPECT_FALSE(resolve(0, true, 100, 20).active);
	const auto result = resolve(1, true, 10, 1);
	EXPECT_EQ(result.percentage, 26);
	EXPECT_EQ(result.deathLordSkeletons, 6);
	EXPECT_EQ(result.graveKnowledgeSkeletons, 20);
	EXPECT_EQ(result.skeletonsOffered, 25 + 6 + 20);
	EXPECT_EQ(resolve(3, true, 100, 20).percentage, 100);
	const auto legacy = resolve(1, false, 0, 0);
	EXPECT_EQ(legacy.percentage, 10);
	EXPECT_EQ(legacy.skeletonsOffered, 9 + 2 + 20);
	const auto conversion = newHorizonsNecromancy::resolve(1, 100, 100, false, false, true,
		true, true, 0, 0, 0, false, true, CreatureID::NONE, {}, {}, false, false, true, 0, 20);
	EXPECT_EQ(conversion.percentage, 30);
	EXPECT_EQ(conversion.skeletonsOffered, 30);
	EXPECT_EQ(conversion.zombiesRaised, 10);
	EXPECT_EQ(conversion.skeletonsRaised, 0);
}

TEST(NewHorizonsIndividualSpecialties, GlobalCodecRetainsCurrentAndRejectsPreviousBeforePrefix)
{
	auto global = riderRules();
	GameSettings source;
	source.addOverride(EGameSettings::HEROES_NEW_HORIZONS, global);
	CMemorySerializer previous;
	previous.oser.version = ESerializationVersion::NEW_HORIZONS_FLY_DAY_END;
	EXPECT_THROW(source.serialize(previous.oser), std::runtime_error);
	EXPECT_TRUE(previous.extractBuffer().empty());
	CMemorySerializer current;
	ASSERT_NO_THROW(source.serialize(current.oser));
	GameSettings decoded;
	ASSERT_NO_THROW(decoded.serialize(current.iser));
	EXPECT_EQ(decoded.getValue(EGameSettings::HEROES_NEW_HORIZONS), global);
	auto legacy = global;
	legacy.Struct().erase("individualSpecialties");
	GameSettings oldSource;
	oldSource.addOverride(EGameSettings::HEROES_NEW_HORIZONS, legacy);
	CMemorySerializer oldWire;
	oldWire.oser.version = ESerializationVersion::NEW_HORIZONS_FLY_DAY_END;
	oldWire.iser.version = oldWire.oser.version;
	ASSERT_NO_THROW(oldSource.serialize(oldWire.oser));
	GameSettings oldDecoded;
	ASSERT_NO_THROW(oldDecoded.serialize(oldWire.iser));
	EXPECT_EQ(oldDecoded.getValue(EGameSettings::HEROES_NEW_HORIZONS), legacy);
	JsonNode raw;
	raw["heroes"]["newHorizons"] = global;
	CMemorySerializer oldRead;
	oldRead.oser & raw;
	oldRead.iser.version = oldWire.oser.version;
	EXPECT_THROW(oldDecoded.serialize(oldRead.iser), std::runtime_error);
	EXPECT_EQ(oldDecoded.getValue(EGameSettings::HEROES_NEW_HORIZONS), legacy);
}


TEST(NewHorizonsIndividualSpecialties, MaximumFormulaPolicyFitsSchoolCastAndDamagePerkScalerBoundary)
{
	auto configured = riderRules();
	configured["individualSpecialties"]["warlocksFormula"]["maximumPercent"].Integer() = 200;
	ASSERT_NO_THROW(validateHeroRules(configured, true));
	const auto captured = resolveHeroRules(configured, HeroClassID(0));
	const auto policy = individualSpecialtyRules(captured);
	ASSERT_TRUE(policy);
	EXPECT_EQ(policy->warlocksFormula.maximumPercent, 200);
	const JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
	const int school = newHorizonsMagic::schoolRankPowerCoefficientPercent(magic, MasteryLevel::EXPERT);
	const int spellcraft = newHorizonsMagic::spellcraftEfficiencyPercent(magic, MasteryLevel::EXPERT);
	const int strengthened = strengthenedSpellcraftBasisPoints(spellcraft, policy->warlocksFormula.maximumPercent);
	// The shared cast coefficient admits at most +200%; damage perks add at most +20%.
	const int combined = school * strengthened * 300 / 10000 * 120 / 100;
	EXPECT_EQ(combined, 99180);
	EXPECT_EQ(spells::scaleSpellPowerComponentWithCoefficientBasisPoints(10000, 1, combined), 99180);
	configured["individualSpecialties"]["warlocksFormula"]["maximumPercent"].Integer() = 201;
	EXPECT_THROW(validateHeroRules(configured, true), std::runtime_error);
	auto malformed = captured;
	malformed["individualSpecialties"]["warlocksFormula"]["maximumPercent"].Integer() = 201;
	EXPECT_THROW(validateResolvedHeroRules(malformed), std::runtime_error);
	EXPECT_THROW(individualSpecialtyRules(malformed), std::runtime_error);
}


TEST(NewHorizonsIndividualSpecialties, EngineSchemaAdmitsBoundaryAndRejectsOutOfRangeOrNullPolicy)
{
	auto rules = riderRules();
	rules["individualSpecialties"]["warlocksFormula"]["maximumPercent"].Integer() = 200;
	EXPECT_TRUE(JsonUtils::validate(rules, "vcmi:newHorizonsHeroes", "individual specialty boundary"));
	rules["individualSpecialties"]["warlocksFormula"]["maximumPercent"].Integer() = 201;
	EXPECT_FALSE(JsonUtils::validate(rules, "vcmi:newHorizonsHeroes", "unsupported individual specialty maximum"));
	rules["individualSpecialties"] = JsonNode();
	EXPECT_FALSE(JsonUtils::validate(rules, "vcmi:newHorizonsHeroes", "null individual specialty policy"));
}
