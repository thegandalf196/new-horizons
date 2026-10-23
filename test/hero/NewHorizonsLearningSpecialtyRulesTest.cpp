/*
 * NewHorizonsLearningSpecialtyRulesTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsHeroRulesFixture.h"
#include "../../lib/GameSettings.h"
#include "../../lib/entities/hero/NewHorizonsLearningSpecialties.h"
#include "../../lib/serializer/CMemorySerializer.h"
using namespace newHorizonsHeroes;
namespace
{
JsonNode learningRules()
{
	auto rules = testHeroRules();
	auto & table = rules["additionalSpecialties"];
	table["version"].Integer() = 1;
	for(const auto key : {"graveArchivist", "spiritTeacher"})
	{
		table[key]["learningPercentPerLevel"].Integer() = 5;
		table[key]["learningMaximumPercent"].Integer() = 100;
	}
	table["graveArchivist"]["additionalEagleEyeSpells"].Integer() = 1;
	table["spiritTeacher"]["mentorExperiencePercent"].Integer() = 25;
	return rules;
}
}
TEST(NewHorizonsLearningSpecialtyRules, CapturedRowsAreIndependentAndHistoricalAbsenceStaysAbsent)
{
	auto configured = learningRules();
	const auto captured = resolveHeroRules(configured, HeroClassID(0));
	ASSERT_TRUE(learningSpecialtyRules(captured, LearningSpecialtyKind::GRAVE_ARCHIVIST));
	EXPECT_EQ(learningSpecialtyRules(captured, LearningSpecialtyKind::GRAVE_ARCHIVIST)->additionalEagleEyeSpells, 1);
	EXPECT_EQ(learningSpecialtyRules(captured, LearningSpecialtyKind::SPIRIT_TEACHER)->mentorExperiencePercent, 25);
	configured["additionalSpecialties"]["spiritTeacher"]["mentorExperiencePercent"].Integer() = 100;
	EXPECT_EQ(learningSpecialtyRules(captured, LearningSpecialtyKind::SPIRIT_TEACHER)->mentorExperiencePercent, 25);
	auto old = captured;
	old["additionalSpecialties"].Struct().erase("graveArchivist");
	old["additionalSpecialties"].Struct().erase("spiritTeacher");
	const auto before = old;
	EXPECT_NO_THROW(validateResolvedHeroRules(old));
	EXPECT_FALSE(learningSpecialtyRules(old, LearningSpecialtyKind::GRAVE_ARCHIVIST));
	EXPECT_FALSE(learningSpecialtyRules(old, LearningSpecialtyKind::SPIRIT_TEACHER));
	EXPECT_EQ(old, before);
}
TEST(NewHorizonsLearningSpecialtyRules, StrictRowsRejectUnsafeMalformedAndBypassInputs)
{
	const auto valid = resolveHeroRules(learningRules(), HeroClassID(0));
	std::vector<JsonNode> invalid(7, valid);
	invalid[0]["additionalSpecialties"]["graveArchivist"] = JsonNode();
	invalid[1]["additionalSpecialties"]["graveArchivist"].Struct().erase("learningMaximumPercent");
	invalid[2]["additionalSpecialties"]["spiritTeacher"]["learningPercentPerLevel"].Integer() = -1;
	invalid[3]["additionalSpecialties"]["graveArchivist"]["additionalEagleEyeSpells"].Integer() = 11;
	invalid[4]["additionalSpecialties"]["spiritTeacher"]["mentorExperiencePercent"].Integer() = 1001;
	invalid[5]["additionalSpecialties"]["graveArchivist"]["learningMaximumPercent"].Integer() = 201;
	invalid[6]["additionalSpecialties"]["spiritTeacher"]["grantMentor"].Bool() = true;
	for(const auto & rules : invalid)
		EXPECT_THROW(validateResolvedHeroRules(rules), std::runtime_error);
}
TEST(NewHorizonsLearningSpecialtyRules, SameUnpublishedCapabilityProtectsBothOptionalRows)
{
	const auto rules = learningRules();
	GameSettings source;
	source.addOverride(EGameSettings::HEROES_NEW_HORIZONS, rules);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_SCHOOL_SPELLCRAFT_SPECIALTIES;
	EXPECT_THROW(source.serialize(old.oser), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	CMemorySerializer current;
	source.serialize(current.oser);
	GameSettings loaded;
	loaded.serialize(current.iser);
	EXPECT_EQ(loaded.getValue(EGameSettings::HEROES_NEW_HORIZONS), rules);
}
