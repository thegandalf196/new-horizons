/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "HotaScriptConverter.h"
#include "MapReaderH3M.h"
#include "CMap.h"
#include "../scripting/nativeMap/EventCodes.h"
#include "../texts/TextIdentifier.h"

HotaScriptConverter::HotaScriptConverter(MapReaderH3M & reader, std::string mapName, LocalizeCallback localizeString)
	: reader(reader), mapName(std::move(mapName)), localizeString(std::move(localizeString)) {}

std::string HotaScriptConverter::eventHandlerName(const std::string & bucket, int eventID)
{
	return bucket + "_" + std::to_string(eventID);
}

void HotaScriptConverter::readScript()
{
	for(const auto bucket : {"heroEvents", "playerEvents", "townEvents", "questEvents"}) loadEventList(bucket);
	for(int i = 0; i < 5; ++i) reader.readInt32(); // next declaration IDs
	loadVariables();
	for(int i = 0; i < 5; ++i) loadEventMap();
}

void HotaScriptConverter::convert(CMap * map, const std::map<si32, ObjectInstanceID> & questIdentifierToId)
{
	JsonNode program;
	program["format"].String() = "new-horizons-native-events";
	program["version"].Integer() = 1;
	program["handlers"] = std::move(events);
	program["variables"] = std::move(variablesTable);
	program["objects"] = loadQuestReferences(map, questIdentifierToId);
	// Keep the existing serialized string slot. No binary save layout changes.
	map->scriptSource = program.toCompactString();
	map->scriptVariableDefinitions = std::move(variables);
}

void HotaScriptConverter::loadEventList(const std::string & bucket)
{
	const int count = reader.readInt32();
	for(int i = 0; i < count; ++i)
	{
		currentBucket = bucket;
		currentEventID = reader.readInt32();
		stringCounter = 0;
		JsonNode & event = events[eventHandlerName(bucket, currentEventID)];
		event["bucket"].String() = bucket;
		event["actions"] = loadActions();
		reader.readBaseString(); // internal editor name
	}
}

void HotaScriptConverter::loadVariables()
{
	const int count = reader.readInt32();
	for(int i = 0; i < count; ++i)
	{
		const int uniqueID = reader.readInt32();
		const auto name = reader.readBaseString();
		auto & definition = variables.emplace_back();
		definition.name = name.empty() ? "var" + std::to_string(uniqueID) : name;
		definition.persistInCampaign = reader.readBool();
		definition.importFromPreviousScenario = reader.readBool();
		definition.initialValue.Integer() = reader.readInt32();
		variablesTable[std::to_string(uniqueID)].String() = definition.name;
	}
}

void HotaScriptConverter::loadEventMap()
{
	const int count = reader.readInt32();
	for(int i = 0; i < count; ++i) reader.readInt32();
}

std::runtime_error HotaScriptConverter::unsupported(const std::string & message) const
{
	return std::runtime_error("Map '" + mapName + "', " + currentBucket + " id " + std::to_string(currentEventID) + ": " + message);
}

std::string HotaScriptConverter::localizedText(const std::string & role)
{
	return localizeString(TextIdentifier(currentBucket, static_cast<size_t>(currentEventID), role, static_cast<size_t>(stringCounter++)));
}

JsonNode HotaScriptConverter::loadImageList(int count)
{
	JsonNode result;
	result.Vector();
	for(int i = 0; i < count; ++i)
	{
		JsonNode image;
		image["type"].Integer() = reader.readInt32();
		image["subtype"].Integer() = reader.readInt32();
		image["value"] = loadExpression();
		result.Vector().push_back(std::move(image));
	}
	return result;
}

std::string HotaScriptConverter::questObjectRef(uint32_t identifier)
{
	referencedObjects.insert(identifier);
	return std::to_string(identifier);
}

JsonNode HotaScriptConverter::loadQuestReferences(CMap * map, const std::map<si32, ObjectInstanceID> & identities)
{
	JsonNode result;
	for(const auto identifier : referencedObjects)
		result[std::to_string(identifier)].String() = map->objects.at(identities.at(identifier).getNum())->instanceName;
	return result;
}
