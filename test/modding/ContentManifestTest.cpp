/*
* ContentManifestTest.cpp, part of VCMI engine
* Authors: listed in file AUTHORS in main folder
* License: GNU General Public License v2.0 or later; see license.txt.
*/
#include "StdInc.h"
#include "../../lib/IHandlerBase.h"
#include "../../lib/json/JsonValidator.h"
#include "../../lib/modding/ContentTypeHandler.h"
#include "../../lib/modding/ModDescription.h"
#include "../../lib/modding/ModScope.h"

namespace
{
class RecordingContentHandler final : public IHandlerBase
{
public:
	size_t loaded = 0;
	std::vector<JsonNode> loadLegacyData() override { return {}; }
	void loadObject(const std::string &, const std::string &, const JsonNode &) override { ++loaded; }
	void loadObject(const std::string &, const std::string &, const JsonNode &, size_t) override { ++loaded; }
};

JsonNode externalManifest()
{
	JsonNode result(JsonMap{});
	result["name"].String() = "Manifest boundary fixture";
	result["modType"].String() = "Test";
	result["version"].String() = "1.0.0";
	result["author"].String() = "VCMI contributors";
	return result;
}
}

TEST(ContentManifestTest, BuiltinGameConfigIsContentListNotExternalManifest)
{
	JsonNode config(JsonPath::builtin("config/gameConfig.json"));
	// Match ModsStorage: required-field exemptions belong to recursively scoped core data.
	config.setModScope(ModScope::scopeBuiltin());
	for(const auto * field : {"name", "modType", "version", "author"})
		ASSERT_FALSE(config.Struct().contains(field)) << field;
	const ModDescription builtin(ModScope::scopeBuiltin(), config, JsonNode());
	CContentHandler loader;
	SCOPED_TRACE(JsonValidator().check(std::string("vcmi:mod"), config));
	EXPECT_TRUE(loader.preloadData(builtin, true));
	EXPECT_TRUE(loader.preloadData(builtin, false));
}

TEST(ContentManifestTest, ExternalMetadataRemainsRequiredRegardlessOfContentValidationMode)
{
	const JsonNode config(JsonMap{});
	for(const auto & id : {std::string("manifestprobe"), std::string("core.foreign")})
	{
		const ModDescription external(id, config, JsonNode());
		CContentHandler loader;
		EXPECT_FALSE(loader.preloadData(external, true)) << id;
		EXPECT_FALSE(loader.preloadData(external, false)) << id;
	}
}

TEST(ContentManifestTest, CompleteExternalManifestRemainsAccepted)
{
	const ModDescription external("manifestprobe", externalManifest(), JsonNode());
	CContentHandler loader;
	EXPECT_TRUE(loader.preloadData(external, true));
	EXPECT_TRUE(loader.preloadData(external, false));
}

TEST(ContentManifestTest, ContentSchemaStillRejectsMalformedBuiltinAndExternalObjects)
{
	for(const auto & scope : {ModScope::scopeBuiltin(), std::string("manifestprobe")})
	{
		RecordingContentHandler recording;
		ContentTypeHandler content(&recording, "skill");
		JsonNode inlineData(JsonMap{});
		inlineData["invalidSkill"]["name"].Integer() = 17;
		ASSERT_TRUE(content.preloadModData(scope, inlineData, true));
		EXPECT_FALSE(content.loadMod(scope, true)) << scope;
		EXPECT_EQ(recording.loaded, 1u);
	}
}
