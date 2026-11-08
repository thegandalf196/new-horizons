/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "LegacyHotaImport.h"
#include "LegacyHotaPrelude.h"
#include "../../constants/EntityIdentifiers.h"
#include "../../GameConstants.h"

namespace scripting::nativeMap
{
JsonNode importLegacyHotaEvents(const std::string & source)
{
	std::vector<std::string> resourceOrder;
	for(int i = 0; i < GameConstants::RESOURCE_QUANTITY; ++i) resourceOrder.push_back(GameResID::encode(i));
	return importLegacyHotaProgram(source, resourceOrder);
}
JsonNode importLegacyHotaProgram(const std::string & source, const std::vector<std::string> & resourceOrder)
{
	using namespace legacy;
	if(resourceOrder.size() != GameConstants::RESOURCE_QUANTITY) unsupported("resource catalogue size");
	if(source.compare(0, 18, "-- generated from ") != 0 || source.find(".h3m HotA event system\n") == std::string::npos)
		unsupported("missing generated HotA format marker");
	Parser parser(source);
	parser.signature("local Map = {} local RESOURCES =");
	const auto resources = parser.expression();
	requireArray(resources, GameConstants::RESOURCE_QUANTITY);
	for(size_t i = 0; i < resources["args"].Vector().size(); ++i)
		if(literal(resources["args"].Vector()[i]) != resourceOrder[i]) unsupported("changed resource order");
	parser.signature(HELPERS);
	JsonNode program;
	program["format"].String() = "new-horizons-native-events"; program["version"].Integer() = 1;
	std::set<std::string> declarations;
	while(parser.accept("local"))
	{
		const auto token = parser.take(); const auto & name = token.text;
		if(token.quoted || !declarations.insert(name).second) unsupported("duplicate or quoted declaration");
		if(name != "Vars" && name != "questObjects") unsupported("unexpected declaration " + name);
		parser.expect("="); const auto table = parser.expression();
		if(table["kind"].String() != "table" || !table["args"].Vector().empty()) unsupported("declaration table shape");
		for(const auto & entry : table["fields"].Struct())
		{
			if(table["keyKinds"][entry.first].String() != (name == "Vars" ? "number" : "string")) unsupported("declaration key type");
			if(name == "questObjects" && (entry.first.size() != 8 || entry.first.find_first_not_of("0123456789abcdef") != std::string::npos)) unsupported("object declaration key");
			const auto key = name == "Vars" ? entry.first : std::to_string(std::stoul(entry.first, nullptr, 16));
			program[name == "Vars" ? "variables" : "objects"][key].String() = literal(entry.second);
		}
	}
	while(parser.accept("function"))
	{
		parser.expect("Map"); parser.expect(":"); const auto token = parser.take(); const auto & name = token.text;
		if(token.quoted || !program["handlers"][name].isNull()) unsupported("quoted or duplicate handler");
		const auto bucket = handlerBucket(name);
		const auto signature = bucket == "townEvents" ? "(game, server, town) local player = town:getOwner()"
			: bucket == "playerEvents" ? "(game, server, player)" : "(game, server, object, hero) local player = hero:getOwner()";
		parser.signature(signature);
		const auto statements = parser.block(); parser.expect("end");
		program["handlers"][name]["bucket"].String() = bucket;
		program["handlers"][name]["actions"] = actions(statements, bucket);
	}
	parser.expect("return"); parser.expect("Map"); parser.expect("<eof>");
	return program;
}
}
