/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "../../Global.h"
#include <gtest/gtest.h>
#include "LegacyMapImportFixtures.h"
#include "../../lib/scripting/nativeMap/LegacyHotaImport.h"

namespace
{
// The source carries catalogue names. Supplying that catalogue is the only
// dependency of pure migration; no resource loader or game-world fixture runs.
const std::vector<std::string> resourceOrder{
	"core:wood", "core:mercury", "core:ore", "core:sulfur", "core:crystal", "core:gems", "core:gold"};

JsonNode importFixture(const std::string & handlers)
{
	return scripting::nativeMap::importLegacyHotaProgram(legacyMapFixtures::generatedProgram(handlers, resourceOrder), resourceOrder);
}

JsonNode json(const std::string & value)
{
	JsonParsingSettings settings;
	settings.strict = true;
	return JsonNode(value.data(), value.size(), settings, "expected native event program");
}
}

TEST(NativeMapImportTest, QuestionBranchesAndFollowingSiblingHaveExactNativeRepresentation)
{
	const auto actual = importFixture(legacyMapFixtures::QUESTION_HANDLERS);
	// A yes/no question encloses two rewards; the gem reward remains its sibling.
	// Comparing the entire data tree detects accidental flattening or reordering.
	const auto expected = json(R"json({
 "format":"new-horizons-native-events","version":1,"variables":{"7":"counter"},
 "handlers":{"heroEvents_1":{"bucket":"heroEvents","actions":[
  {"op":12,"text":"question","images":[],
   "yes":[{"op":4,"amounts":[0,0,0,0,0,0,100]}],
   "no":[{"op":4,"amounts":[0,0,0,0,0,0,5]}]},
  {"op":4,"amounts":[0,0,0,0,0,1,0]}
 ]}}})json");
	EXPECT_EQ(actual, expected);
	EXPECT_EQ(importFixture(legacyMapFixtures::QUESTION_HANDLERS), actual);
}

TEST(NativeMapImportTest, LoopBoundsAndVariableAssignmentRemainDistinctAcrossBlockingRewards)
{
	const auto actual = importFixture(legacyMapFixtures::NESTED_HANDLERS);
	const auto expected = json(R"json({
 "format":"new-horizons-native-events","version":1,"variables":{"7":"counter"},"handlers":{
 "heroEvents_1":{"bucket":"heroEvents","actions":[
  {"op":28,"variable":7,"from":1,"to":{"op":16,"left":2,"right":3},"body":[
   {"op":1,"branches":[
    {"condition":{"op":1,"args":[{"op":6,"condition":{"op":0,"value":false}},
     {"op":2,"args":[{"op":8,"left":{"op":10},"right":0},{"op":3,"left":{"op":12},"right":2}]}]},
     "body":[{"op":3,"variable":7,"mode":2,"value":{"op":3,"left":{"op":1,"variable":7},
      "right":{"op":7,"left":{"op":2,"value":5},"right":2}}}]},
    {"condition":{"op":7,"entity":"core:spellbook"},"body":[{"op":22,"take":false,"amount":-2}]},
    {"condition":{"op":0,"value":true},"body":[{"op":21,"take":false,"amount":3}]}
   ]}]},
  {"op":6,"text":"reward","body":[{"op":9,"take":false,"entity":"","scroll":"core:magicArrow"}]},
  {"op":24,"handler":"heroEvents_2"}
 ]},
 "heroEvents_2":{"bucket":"heroEvents","actions":[
  {"op":29,"text":"message","images":[{"type":2,"subtype":6,"value":{"op":8,"left":5,"right":{"op":2,"value":2}}}]}
 ]}}})json");
	EXPECT_EQ(actual, expected);
}

TEST(NativeMapImportTest, CompleteGeneratedSignaturesRetainAllFourContexts)
{
	const auto program = importFixture(legacyMapFixtures::COMPLETE_HANDLERS);
	const auto & handlers = program["handlers"];
	ASSERT_EQ(handlers.Struct().size(), 6u);
	EXPECT_EQ(handlers["heroEvents_1"]["bucket"].String(), "heroEvents");
	EXPECT_EQ(handlers["questEvents_2"]["bucket"].String(), "questEvents");
	EXPECT_EQ(handlers["townEvents_1"]["bucket"].String(), "townEvents");
	EXPECT_EQ(handlers["playerEvents_1"]["bucket"].String(), "playerEvents");
	const auto & hero = handlers["heroEvents_1"]["actions"].Vector();
	ASSERT_EQ(hero.size(), 8u);
	EXPECT_EQ(hero[1]["amount"]["op"].Integer(), 10);
	EXPECT_EQ(hero[2]["amount"]["op"].Integer(), 11);
	EXPECT_EQ(hero[3]["amount"]["op"].Integer(), 12);
	EXPECT_EQ(hero[6]["showInLog"].Bool(), false);
	EXPECT_EQ(hero[7]["handler"].String(), "questEvents_2");
	const auto & quest = handlers["questEvents_2"]["actions"].Vector().at(0);
	EXPECT_EQ(quest["proposal"].String(), "proposal");
	EXPECT_EQ(quest["body"].Vector().at(0)["quest"].Bool(), true);
	const auto & town = handlers["townEvents_1"]["actions"].Vector();
	ASSERT_EQ(town.size(), 3u);
	EXPECT_EQ(town[0]["level"].Integer(), 0);
	EXPECT_EQ(town[0]["amount"].Integer(), 7);
	EXPECT_EQ(town[2]["handler"].String(), "townEvents_2");
	EXPECT_EQ(handlers["playerEvents_1"]["actions"].Vector().at(1)["handler"].String(), "playerEvents_2");
}

TEST(NativeMapImportTest, PrimarySkillEnumsAndObjectIdentitiesNeedNoContentCatalogue)
{
	const auto program = importFixture(R"lua(
local questObjects = {["0000002a"] = "town_target"}
function Map:heroEvents_1(game, server, object, hero)
 local player = hero:getOwner()
 server:grantPrimarySkill(hero, ENUM.PrimarySkill.attack, hero:getPrimarySkill(ENUM.PrimarySkill.knowledge))
 if playerOwnsTown(game, resolvePlayer(-1, player), questObjects["0000002a"]) then end
end
)lua");
	EXPECT_EQ(program["objects"]["42"].String(), "town_target");
	const auto & actions = program["handlers"]["heroEvents_1"]["actions"].Vector();
	ASSERT_EQ(actions.size(), 2u);
	EXPECT_EQ(actions[0]["skill"].Integer(), 0);
	EXPECT_EQ(actions[0]["amount"]["skill"].Integer(), 3);
	EXPECT_EQ(actions[1]["branches"].Vector().at(0)["condition"]["object"].String(), "42");
}

TEST(NativeMapImportTest, RejectsAllChangedSignaturesInsteadOfDiscardingInputs)
{
	const auto & rejected = legacyMapFixtures::rejectedStatements();
	ASSERT_EQ(rejected.size(), 38u);
	for(const auto & statement : rejected)
	{
		SCOPED_TRACE(statement);
		const auto handler = "function Map:heroEvents_1(game, server, object, hero)\n local player = hero:getOwner()\n" + statement + "\nend\n";
		EXPECT_THROW(importFixture(handler), std::runtime_error);
	}
}

TEST(NativeMapImportTest, RejectsModifiedHelpersAndResourceOrdering)
{
	const auto source = legacyMapFixtures::generatedProgram(legacyMapFixtures::QUESTION_HANDLERS, resourceOrder);
	auto modified = source;
	const auto begin = modified.find("if player < 0"); ASSERT_NE(begin, std::string::npos);
	modified.replace(begin, std::string("if player < 0").size(), "if player < 1");
	EXPECT_THROW(scripting::nativeMap::importLegacyHotaProgram(modified, resourceOrder), std::runtime_error);
	auto wrongOrder = resourceOrder;
	std::swap(wrongOrder.front(), wrongOrder.back());
	EXPECT_THROW(scripting::nativeMap::importLegacyHotaProgram(source, wrongOrder), std::runtime_error);
	EXPECT_THROW(scripting::nativeMap::importLegacyHotaProgram(source, {}), std::runtime_error);
	EXPECT_THROW(scripting::nativeMap::importLegacyHotaProgram("local Map = {} return Map", resourceOrder), std::runtime_error);
}

TEST(NativeMapImportTest, RejectsDeclarationTypeChangesAndDuplicateFields)
{
	const auto source = legacyMapFixtures::generatedProgram(legacyMapFixtures::QUESTION_HANDLERS, resourceOrder);
	auto wrongKey = source;
	const auto begin = wrongKey.find("[7] = \"counter\""); ASSERT_NE(begin, std::string::npos);
	wrongKey.replace(begin, std::string("[7]").size(), "[\"7\"]");
	EXPECT_THROW(scripting::nativeMap::importLegacyHotaProgram(wrongKey, resourceOrder), std::runtime_error);
	EXPECT_THROW(importFixture(R"lua(function Map:heroEvents_1(game, server, object, hero)
 local player = hero:getOwner()
 server:showMessage(player, {append={"text"},append={"changed"}}, toComponents({}))
end)lua"), std::runtime_error);
}
