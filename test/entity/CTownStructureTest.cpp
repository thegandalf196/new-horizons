/*
 * CTownStructureTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../OriginalDataRequirement.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/entities/faction/CFaction.h"
#include "../../lib/entities/faction/CTown.h"
#include "../../lib/entities/faction/CTownHandler.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/json/JsonUtils.h"
#include "../../lib/modding/ModScope.h"

namespace
{
JsonNode structureWithFrameMode(const JsonNode & currentFrameOnly)
{
	JsonNode structure;
	structure["animation"].String() = "TBCSBLAK.def";
	structure["x"].Float() = 213;
	structure["y"].Float() = 251;
	structure["currentFrameOnly"] = currentFrameOnly;
	// the animation format check looks the file up among the original Heroes III data
	structure.setModScope(ModScope::scopeBuiltin());
	return structure;
}

bool validStructure(const JsonNode & structure)
{
	return JsonUtils::validate(structure, "vcmi:townStructure", "town structure frame mode");
}
}

TEST(CTownStructureTest, SchemaAcceptsOnlyBooleanCurrentFrameOnly)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();

	EXPECT_TRUE(validStructure(structureWithFrameMode(JsonNode(true))));
	EXPECT_TRUE(validStructure(structureWithFrameMode(JsonNode(false))));
	EXPECT_FALSE(validStructure(structureWithFrameMode(JsonNode("true"))));
	EXPECT_FALSE(validStructure(structureWithFrameMode(JsonNode(1))));

	JsonNode withoutOption = structureWithFrameMode(JsonNode(true));
	withoutOption.Struct().erase("currentFrameOnly");
	EXPECT_TRUE(validStructure(withoutOption));
}

// The vcmi-test fixture mod marks only Castle's blacksmith; every other structure keeps the
// original drawing, the first frame of its range beneath the current one. Tests load the TEST
// preset without New Horizons, so Academy's opted-in structures are checked by the Python
// Academy town animation art test instead.
TEST(CTownStructureTest, OnlyMarkedStructuresDrawCurrentFrameOnly)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();

	size_t marked = 0;
	size_t checked = 0;
	for(const auto & faction : LIBRARY->townh->objects)
	{
		if(!faction || !faction->town)
			continue;
		for(const auto & structure : faction->town->clientInfo.structures)
		{
			++checked;
			const bool fixture = faction->getId() == FactionID::CASTLE && structure->identifier == "blacksmith";
			EXPECT_EQ(structure->currentFrameOnly, fixture) << faction->getJsonKey() << " " << structure->identifier;
			if(structure->currentFrameOnly)
				++marked;
		}
	}
	EXPECT_EQ(marked, 1);
	EXPECT_GT(checked, 100);
}
