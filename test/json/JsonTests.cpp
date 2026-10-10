#include "StdInc.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/json/JsonUtils.h"
#include "../../lib/json/JsonValidator.h"
#include "../../lib/json/JsonBonus.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/modding/ModScope.h"

TEST(JsonSchemaStartupTest, PatternSearchAndAnchorsRemainDistinct)
{
	JsonValidator validator;
	JsonNode schema;
	schema["type"].String() = "string";
	schema["pattern"].String() = "[A-Z]+";
	EXPECT_TRUE(validator.check(schema, JsonNode("before ABC after")).empty());
	EXPECT_FALSE(validator.check(schema, JsonNode("lowercase")).empty());
	schema["pattern"].String() = "^[A-Z]+$";
	EXPECT_FALSE(validator.check(schema, JsonNode("before ABC after")).empty());
	EXPECT_TRUE(validator.check(schema, JsonNode("ABC")).empty());
}

TEST(JsonSchemaStartupTest, MalformedPatternIsReportedWithoutThrowing)
{
	JsonValidator validator;
	JsonNode schema;
	schema["pattern"].String() = "[";
	EXPECT_NE(validator.check(schema, JsonNode("status")).find("Invalid regular expression"), std::string::npos);
	schema["pattern"] = JsonNode(1);
	EXPECT_NE(validator.check(schema, JsonNode("status")).find("must be a string"), std::string::npos);
}

TEST(JsonSchemaStartupTest, AuthoredBonusIdentityPatternRejectsControlCharacters)
{
	JsonValidator validator;
	const auto schema = JsonUtils::getSchema("vcmi:bonusInstance")["properties"]["statusIdentity"];
	EXPECT_TRUE(validator.check(schema, JsonNode("core:curse")).empty());
	EXPECT_TRUE(validator.check(schema, JsonNode("")).empty());
	for(const char control : { '\0', '\n', '\x1f', '\x7f' })
	{
		const std::string identity = std::string("core:") + control + "curse";
		EXPECT_FALSE(validator.check(schema, JsonNode(identity)).empty());
	}
	EXPECT_FALSE(validator.check(schema, JsonNode(std::string(257, 'x'))).empty());
}

TEST(JsonSchemaStartupTest, NullShootSoundClearsInheritedMeleeSoundWithoutAcceptingBadValues)
{
	JsonValidator validator;
	const auto schema = JsonUtils::getSchema("vcmi:creature")["properties"]["sound"]["properties"]["shoot"];
	EXPECT_TRUE(validator.check(schema, JsonNode()).empty());
	EXPECT_FALSE(validator.check(schema, JsonNode(12)).empty());
	EXPECT_FALSE(validator.check(schema, JsonNode(true)).empty());
	// A string still has to name a real sound resource; the null branch cannot hide it.
	JsonNode missingSound("missing-schema-test-sound-87c336.wav");
	missingSound.setModScope(ModScope::scopeBuiltin());
	EXPECT_FALSE(validator.check(schema, missingSound).empty());
}

TEST(JsonSchemaStartupTest, TaggedBonusValidatesAndStillUsesStrictBonusParser)
{
	JsonNode bonus;
	bonus["type"].String() = "STACKS_SPEED";
	bonus["val"].Integer() = -1;
	bonus["duration"].String() = "N_TURNS";
	bonus["turns"].Integer() = 2;
	bonus["statusTags"].Vector().emplace_back("DEBUFF");
	bonus["statusIdentity"].String() = "core:slow";
	JsonValidator validator;
	EXPECT_TRUE(validator.check("vcmi:bonusInstance", bonus).empty());
	const auto parsed = JsonUtils::parseBonus(bonus);
	ASSERT_NE(parsed, nullptr);
	EXPECT_EQ(parsed->statusIdentity, "core:slow");
	EXPECT_EQ(parsed->statusTags, std::vector<BonusStatusTag>{ BonusStatusTag::DEBUFF });

	bonus["statusIdentity"].String() = "core:\nslow";
	EXPECT_FALSE(validator.check("vcmi:bonusInstance", bonus).empty());
	EXPECT_THROW(JsonUtils::parseBonus(bonus), std::runtime_error);
}

TEST(JsonTest, conflictDetectionTestNoConflict)
{
	constexpr char textA[] = R"({ "keyA" : 1, "sameKey" : { "keyInner" : 5 } })";
	constexpr char textB[] = R"({ "keyB" : 1, "sameKey" : { "keyInner" : 10 } })";

	JsonNode jsonA(textA, std::size(textA), "Test A");
	JsonNode jsonB(textB, std::size(textB), "Test B");
	JsonNode result;

	jsonA.setModScope("modA");
	jsonB.setModScope("modB");

	JsonUtils::detectConflicts(result, jsonA, jsonB, "test");

	EXPECT_EQ(result.Struct().size(), 1);
	EXPECT_EQ(result.Struct().count("test/sameKey/keyInner"), 1);
	EXPECT_EQ(result["test/sameKey/keyInner"].Struct().size(), 1);
}

TEST(JsonTest, conflictDetectionTestSimpleConflict)
{
	constexpr char textA[] = R"({ "keyA" : 1, "sameKey" : { "keyInner" : 5 } })";
	constexpr char textB[] = R"({ "keyB" : 1, "sameKey" : { "keyInner" : 10 } })";
	constexpr char textC[] = R"({ "keyC" : 1, "sameKey" : { "keyInner" : 15 } })";

	JsonNode jsonA(textA, std::size(textA), "Test A");
	JsonNode jsonB(textB, std::size(textB), "Test B");
	JsonNode jsonC(textC, std::size(textC), "Test C");
	JsonNode result;

	jsonA.setModScope("modA");
	jsonB.setModScope("modB");
	jsonC.setModScope("modC");

	JsonUtils::detectConflicts(result, jsonA, jsonB, "test");
	JsonUtils::detectConflicts(result, jsonA, jsonC, "test");

	EXPECT_EQ(result.Struct().size(), 1);
	EXPECT_EQ(result.Struct().count("test/sameKey/keyInner"), 1);
	EXPECT_EQ(result["test/sameKey/keyInner"].Struct().size(), 2);
}

TEST(JsonTest, conflictDetectionTestArrayConflict)
{
	constexpr char textA[] = R"({ "keyA" : 1, "sameKey" : { "keyInner" : [ 10 ] } })";
	constexpr char textB[] = R"({ "keyB" : 1, "sameKey" : { "keyInner" : [ 20 ] } })";
	constexpr char textC[] = R"({ "keyC" : 1, "sameKey" : { "keyInner" : [ 30 ] } })";

	JsonNode jsonA(textA, std::size(textA), "Test A");
	JsonNode jsonB(textB, std::size(textB), "Test B");
	JsonNode jsonC(textC, std::size(textC), "Test C");
	JsonNode result;

	jsonA.setModScope("modA");
	jsonB.setModScope("modB");
	jsonC.setModScope("modC");

	JsonUtils::detectConflicts(result, jsonA, jsonB, "test");
	JsonUtils::detectConflicts(result, jsonA, jsonC, "test");

	EXPECT_EQ(result.Struct().size(), 1);
	EXPECT_EQ(result.Struct().count("test/sameKey/keyInner"), 1);
	EXPECT_EQ(result["test/sameKey/keyInner"].Struct().size(), 2);
}

TEST(JsonTest, conflictDetectionTestArrayModifyConflict)
{
	constexpr char textA[] = R"({ "keyA" : 1, "sameKey" : { "keyInner" : [ 10, 20 ] } })";
	constexpr char textB[] = R"({ "keyB" : 1, "sameKey" : { "keyInner" : { "modify@1" : 20 } })";
	constexpr char textC[] = R"({ "keyC" : 1, "sameKey" : { "keyInner" : { "modify@1" : 30 } })";

	JsonNode jsonA(textA, std::size(textA), "Test A");
	JsonNode jsonB(textB, std::size(textB), "Test B");
	JsonNode jsonC(textC, std::size(textC), "Test C");
	JsonNode result;

	jsonA.setModScope("modA");
	jsonB.setModScope("modB");
	jsonC.setModScope("modC");

	JsonUtils::detectConflicts(result, jsonA, jsonB, "test");
	JsonUtils::detectConflicts(result, jsonA, jsonC, "test");

	EXPECT_EQ(result.Struct().size(), 1);
	EXPECT_EQ(result.Struct().count("test/sameKey/keyInner/1"), 1);
	EXPECT_EQ(result["test/sameKey/keyInner/1"].Struct().size(), 2);
}

TEST(JsonTest, conflictDetectionTestArrayModifySafe)
{
	constexpr char textA[] = R"({ "keyA" : 1, "sameKey" : { "keyInner" : [ 10, 20 ] } })";
	constexpr char textB[] = R"({ "keyB" : 1, "sameKey" : { "keyInner" : { "modify@1" : 20 } })";
	constexpr char textC[] = R"({ "keyC" : 1, "sameKey" : { "keyInner" : { "modify@2" : 30 } })";

	JsonNode jsonA(textA, std::size(textA), "Test A");
	JsonNode jsonB(textB, std::size(textB), "Test B");
	JsonNode jsonC(textC, std::size(textC), "Test C");
	JsonNode result;

	jsonA.setModScope("modA");
	jsonB.setModScope("modB");
	jsonC.setModScope("modC");

	JsonUtils::detectConflicts(result, jsonA, jsonB, "test");
	JsonUtils::detectConflicts(result, jsonA, jsonC, "test");

	EXPECT_EQ(result.Struct().size(), 2);
	EXPECT_EQ(result.Struct().count("test/sameKey/keyInner/1"), 1);
	EXPECT_EQ(result.Struct().count("test/sameKey/keyInner/2"), 1);
	EXPECT_EQ(result["test/sameKey/keyInner/1"].Struct().size(), 1);
	EXPECT_EQ(result["test/sameKey/keyInner/2"].Struct().size(), 1);
}

TEST(JsonTest, conflictDetectionTestArrayAppendAlwaysSafe)
{
	constexpr char textA[] = R"({ "keyA" : 1, "sameKey" : { "keyInner" : [ 10, 20 ] } })";
	constexpr char textB[] = R"({ "keyB" : 1, "sameKey" : { "keyInner" : { "append" : 20 } })";
	constexpr char textC[] = R"({ "keyC" : 1, "sameKey" : { "keyInner" : { "append" : 30 } })";

	JsonNode jsonA(textA, std::size(textA), "Test A");
	JsonNode jsonB(textB, std::size(textB), "Test B");
	JsonNode jsonC(textC, std::size(textC), "Test C");
	JsonNode result;

	jsonA.setModScope("modA");
	jsonB.setModScope("modB");
	jsonC.setModScope("modC");

	JsonUtils::detectConflicts(result, jsonA, jsonB, "test");
	JsonUtils::detectConflicts(result, jsonA, jsonC, "test");

	EXPECT_EQ(result.Struct().size(), 0);
}

/// A script declares what its kind needs: every one of them a schema, and a combat event script
/// also the description and the priority that decide how its ability reads and when it runs.
class ScriptSchemaTest : public ::testing::Test
{
public:
	static JsonNode scriptNode(const std::string & text)
	{
		JsonNode node(text.data(), text.size(), "ScriptSchemaTest");
		// core is exempt from required-entry checks, so the sample has to come from somewhere else
		node.setModScope("vcmi-test");
		return node;
	}

	static bool isValid(const std::string & text)
	{
		return JsonUtils::validate(scriptNode(text), "vcmi:script", "ScriptSchemaTest");
	}
};

TEST_F(ScriptSchemaTest, combatEventScriptDeclaresEverythingItsKindNeeds)
{
	EXPECT_TRUE(isValid(R"({
		"implements" : "combatEvent",
		"script" : "combat/spikes",
		"patches" : [ ],
		"priority" : 0,
		"schema" : { "properties" : {}, "additionalProperties" : false },
		"description" : "{Spikes}"
	})"));
}

TEST_F(ScriptSchemaTest, combatEventScriptWithoutPriorityIsRejected)
{
	EXPECT_FALSE(isValid(R"({
		"implements" : "combatEvent",
		"script" : "combat/spikes",
		"patches" : [ ],
		"schema" : { "properties" : {}, "additionalProperties" : false },
		"description" : "{Spikes}"
	})"));
}

TEST_F(ScriptSchemaTest, combatEventScriptWithoutDescriptionIsRejected)
{
	EXPECT_FALSE(isValid(R"({
		"implements" : "combatEvent",
		"script" : "combat/spikes",
		"patches" : [ ],
		"priority" : 0,
		"schema" : { "properties" : {}, "additionalProperties" : false }
	})"));
}

TEST_F(ScriptSchemaTest, scriptWithoutSchemaIsRejected)
{
	EXPECT_FALSE(isValid(R"({
		"implements" : "spellEffect",
		"script" : "spells/spikes",
		"patches" : [ ]
	})"));
}

TEST_F(ScriptSchemaTest, spellEffectNeedsNoDescriptionOrPriority)
{
	EXPECT_TRUE(isValid(R"({
		"implements" : "spellEffect",
		"script" : "spells/spikes",
		"patches" : [ ],
		"schema" : { "properties" : {}, "additionalProperties" : false }
	})"));
}

TEST(JsonTest, floatsMatchTheNearestDouble)
{
	constexpr char text[] = R"({ "a" : 0.7, "b" : 1.75, "c" : -0.15, "d" : 1234.5678, "e" : 1e3 })";

	JsonNode json(text, std::size(text), "Test");

	EXPECT_EQ(json["a"].Float(), 0.7);
	EXPECT_EQ(json["b"].Float(), 1.75);
	EXPECT_EQ(json["c"].Float(), -0.15);
	EXPECT_EQ(json["d"].Float(), 1234.5678);
	EXPECT_EQ(json["e"].Float(), 1e3);
}

TEST(JsonTest, floatsWithMoreDigitsThanADoubleHoldsDoNotOverflow)
{
	constexpr char text[] = R"({ "a" : 0.12345678901234567890123456789, "b" : 9007199254740993.5 })";

	JsonNode json(text, std::size(text), "Test");

	// digits past what a double holds are dropped, so the value is only near - what matters is that
	// gathering them never overflows the mantissa into a number of the wrong magnitude or sign
	EXPECT_NEAR(json["a"].Float() / 0.12345678901234567890123456789, 1.0, 1e-15);
	EXPECT_NEAR(json["b"].Float() / 9007199254740993.5, 1.0, 1e-15);
}
