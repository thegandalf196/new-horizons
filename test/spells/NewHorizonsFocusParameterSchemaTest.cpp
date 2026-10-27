/*
 * NewHorizonsFocusParameterSchemaTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/constants/StringConstants.h"
#include "../../lib/json/JsonValidator.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/modding/IdentifierStorage.h"
#include "../../lib/modding/ModScope.h"
#include "../../lib/scripting/ScriptService.h"

namespace
{
class NewHorizonsFocusParameterSchemaTest : public testing::Test
{
protected:
	void SetUp() override
	{
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	JsonNode schema(const std::string & key)
	{
		const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "script", key);
		EXPECT_TRUE(id.has_value());
		return id ? LIBRARY->scriptTypes()->getById(ScriptID(*id)).parametersSchema : JsonNode();
	}
};
}

TEST_F(NewHorizonsFocusParameterSchemaTest, BothSchemasAcceptExactIntegerAndLuaFloatingSides)
{
	for(const auto * key : {"core:focusMagic", "core:arcaneBreach"})
	{
		SCOPED_TRACE(key);
		const auto parametersSchema = schema(key);
		ASSERT_TRUE(parametersSchema.isStruct());
		for(const auto & side : {JsonNode(0), JsonNode(1), JsonNode(0.0), JsonNode(1.0)})
		{
			SCOPED_TRACE(static_cast<int>(side.getType()));
			JsonNode parameters;
			parameters["beneficiarySide"] = side;
			EXPECT_TRUE(JsonValidator().check(parametersSchema, parameters).empty());
		}
	}
}

TEST_F(NewHorizonsFocusParameterSchemaTest, BothSchemasRejectEveryNonSideRepresentation)
{
	for(const auto * key : {"core:focusMagic", "core:arcaneBreach"})
	{
		SCOPED_TRACE(key);
		const auto parametersSchema = schema(key);
		ASSERT_TRUE(parametersSchema.isStruct());
		JsonNode missing;
		missing.setType(JsonNode::JsonType::DATA_STRUCT);
		EXPECT_FALSE(JsonValidator().check(parametersSchema, missing).empty());
		for(const auto & side : {JsonNode(), JsonNode(-1), JsonNode(2), JsonNode(-1.0), JsonNode(2.0),
			JsonNode(0.5), JsonNode(-0.5), JsonNode(1.5), JsonNode("0"), JsonNode("1"), JsonNode(false), JsonNode(true)})
		{
			SCOPED_TRACE(side.toCompactString());
			JsonNode parameters;
			parameters["beneficiarySide"] = side;
			EXPECT_FALSE(JsonValidator().check(parametersSchema, parameters).empty());
		}
	}
}
