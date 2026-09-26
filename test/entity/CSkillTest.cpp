/*
 * CSkillTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../../lib/CSkillHandler.h"
#include "../../lib/json/JsonUtils.h"

namespace test
{

using namespace ::testing;

namespace
{

class TestSkillHandler final : public CSkillHandler
{
public:
	using CSkillHandler::loadFromJson;
};

JsonNode skillConfig(const std::string & combatStatusProvider = {})
{
	JsonNode skill;
	skill["name"].String() = "Combat status probe";
	skill["specialty"].Vector();
	skill["gainChance"]["might"].Integer() = 0;
	skill["gainChance"]["magic"].Integer() = 0;
	skill["tags"].Struct();
	skill["onlyOnWaterMap"].Bool() = false;
	skill["special"].Bool() = false;
	skill["obligatoryMajor"].Bool() = false;
	skill["obligatoryMinor"].Bool() = false;

	for(const std::string level : {"basic", "advanced", "expert"})
	{
		skill[level]["description"].String() = level + " description";
		skill[level]["effects"].Struct();
		skill[level]["images"]["small"].String() = "probe-small.png";
		skill[level]["images"]["medium"].String() = "probe-medium.png";
		skill[level]["images"]["large"].String() = "probe-large.png";
		skill[level]["images"]["scenarioBonus"].String() = "probe-scenario.png";
	}

	if(!combatStatusProvider.empty())
	{
		skill["combatStatus"]["provider"].String() = combatStatusProvider;
		skill["combatStatus"]["description"].String() = "Localized combat status probe";
	}
	return skill;
}

bool validSkillSchema(const JsonNode & skill)
{
	return JsonUtils::validate(skill, "vcmi:skill", "native skill combat-status schema proof");
}

}

class CSkillTest : public Test
{
public:
	MOCK_METHOD4(registarCb, void(int32_t, int32_t, const std::string &, const std::string &));

protected:
	std::shared_ptr<CSkill> subject;

	void SetUp() override
	{
		subject = std::make_shared<CSkill>(SecondarySkill(42));
	}
};

TEST_F(CSkillTest, RegistersIcons)
{
	for(int level = 1; level <= 3; level++)
	{
		CSkill::LevelInfo & skillAtLevel = subject->at(level);

		skillAtLevel.iconSmall = "TestS"+std::to_string(level);
		skillAtLevel.iconMedium = "TestM"+std::to_string(level);
		skillAtLevel.iconLarge = "TestL"+std::to_string(level);
	}

	auto cb = [this](auto && PH1, auto && PH2, auto && PH3, auto && PH4) 
	{
		registarCb(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2), std::forward<decltype(PH3)>(PH3), std::forward<decltype(PH4)>(PH4));
	};

	for(int level = 1; level <= 3; level++)
	{
		int frame = 2 + level + 3 * 42;

		EXPECT_CALL(*this, registarCb(Eq(frame), Eq(0), "SECSK32", "TestS"+std::to_string(level)));
		EXPECT_CALL(*this, registarCb(Eq(frame), Eq(0), "SECSKILL", "TestM"+std::to_string(level)));
		EXPECT_CALL(*this, registarCb(Eq(frame), Eq(0), "SECSK82", "TestL"+std::to_string(level)));
	}

	subject->registerIcons(cb);
}

TEST_F(CSkillTest, MissingCombatStatusDefaultsToNoneAndEmptyTooltip)
{
	EXPECT_EQ(subject->getCombatStatusProvider(), CSkill::CombatStatusProvider::NONE);
	EXPECT_TRUE(subject->getCombatStatusDescriptionTranslated().empty());

	TestSkillHandler handler;
	const auto parsed = handler.loadFromJson("vcmi-test", skillConfig(), "combatStatusOmittedProbe", 42);
	EXPECT_EQ(parsed->getCombatStatusProvider(), CSkill::CombatStatusProvider::NONE);
	EXPECT_TRUE(parsed->getCombatStatusDescriptionTranslated().empty());
}

TEST_F(CSkillTest, CombatStatusDescriptionIsRegisteredForTranslation)
{
	TestSkillHandler handler;
	const auto parsed = handler.loadFromJson("vcmi-test", skillConfig("metamagicUses"), "combatStatusTranslatedProbe", 42);
	EXPECT_EQ(parsed->getCombatStatusProvider(), CSkill::CombatStatusProvider::METAMAGIC_USES);
	EXPECT_EQ(parsed->getCombatStatusDescriptionTranslated(), "Localized combat status probe");
}

TEST_F(CSkillTest, BloodrageCombatStatusProviderIsTyped)
{
	TestSkillHandler handler;
	const auto parsed = handler.loadFromJson("vcmi-test", skillConfig("bloodrageDamage"), "bloodrageCombatStatusProbe", 42);
	EXPECT_EQ(parsed->getCombatStatusProvider(), CSkill::CombatStatusProvider::BLOODRAGE_DAMAGE);
	EXPECT_EQ(parsed->getCombatStatusDescriptionTranslated(), "Localized combat status probe");
}

TEST(CSkillCombatStatusSchemaTest, OptionalStatusAcceptsCanonicalAndRejectsMalformedShapes)
{
	JsonNode skills(JsonPath::builtin("config/newHorizonsSkills"));
	// This test validates status metadata, not mod-mounted image resources.
	// Images are optional; their bindings are checked by the skill-data suite.
	for(const auto * identifier : {"metamagic", "bloodrage"})
	{
		JsonNode canonicalSkill = skills[identifier];
		for(const auto * rank : {"basic", "advanced", "expert"})
			canonicalSkill[rank].Struct().erase("images");
		ASSERT_TRUE(validSkillSchema(canonicalSkill)) << identifier;
	}
	JsonNode canonical = skills["metamagic"];
	for(const auto * rank : {"basic", "advanced", "expert"})
		canonical[rank].Struct().erase("images");

	JsonNode invalidObjectType = canonical;
	invalidObjectType["combatStatus"] = JsonNode("metamagicUses");
	EXPECT_FALSE(validSkillSchema(invalidObjectType));

	JsonNode invalidProvider = canonical;
	invalidProvider["combatStatus"]["provider"].String() = "unknownProvider";
	EXPECT_FALSE(validSkillSchema(invalidProvider));

	JsonNode invalidDescriptionType = canonical;
	invalidDescriptionType["combatStatus"]["description"].Integer() = 7;
	EXPECT_FALSE(validSkillSchema(invalidDescriptionType));

	JsonNode missingDescription = canonical;
	missingDescription["combatStatus"].Struct().erase("description");
	EXPECT_FALSE(validSkillSchema(missingDescription));

	JsonNode omittedStatus = canonical;
	omittedStatus.Struct().erase("combatStatus");
	EXPECT_TRUE(validSkillSchema(omittedStatus));
}

TEST(CSkillCombatStatusLoaderTest, RejectsMalformedMetadataBeforeLoadingSkillData)
{
	TestSkillHandler handler;
	std::vector<JsonNode> invalidStatuses;
	invalidStatuses.emplace_back(); // explicit null differs from an omitted key
	invalidStatuses.emplace_back("metamagicUses");
	JsonNode arrayStatus;
	arrayStatus.Vector();
	invalidStatuses.push_back(arrayStatus);
	invalidStatuses.emplace_back(JsonMap{}); // missing provider and description

	JsonNode unknownProvider(JsonMap{});
	unknownProvider["provider"].String() = "unknownProvider";
	unknownProvider["description"].String() = "Description";
	invalidStatuses.push_back(unknownProvider);

	JsonNode missingProvider(JsonMap{});
	missingProvider["description"].String() = "Description";
	invalidStatuses.push_back(missingProvider);

	JsonNode invalidDescriptionType(JsonMap{});
	invalidDescriptionType["provider"].String() = "metamagicUses";
	invalidDescriptionType["description"].Integer() = 9;
	invalidStatuses.push_back(invalidDescriptionType);

	JsonNode missingDescription(JsonMap{});
	missingDescription["provider"].String() = "metamagicUses";
	invalidStatuses.push_back(missingDescription);

	JsonNode emptyDescription(JsonMap{});
	emptyDescription["provider"].String() = "metamagicUses";
	emptyDescription["description"].String().clear();
	invalidStatuses.push_back(emptyDescription);

	JsonNode unknownField(JsonMap{});
	unknownField["provider"].String() = "metamagicUses";
	unknownField["description"].String() = "Description";
	unknownField["extra"].Bool() = true;
	invalidStatuses.push_back(unknownField);

	for(size_t index = 0; index < invalidStatuses.size(); ++index)
	{
		SCOPED_TRACE(index);
		JsonNode skill = skillConfig();
		skill["combatStatus"] = invalidStatuses[index];
		EXPECT_THROW(handler.loadFromJson("vcmi-test", skill, "invalidCombatStatusProbe", 42), std::runtime_error);
	}
}

}
