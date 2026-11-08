/*
 * HotaScriptConverter.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#pragma once

#include "../scripting/ScriptVariablesStorage.h"
#include "../constants/EntityIdentifiers.h"
#include "../json/JsonNode.h"

class MapReaderH3M;
class TextIdentifier;
class CMap;

/// Converts the HotA (Horn of the Abyss) event-scripting bytecode embedded in a
/// H3M map into a versioned native event program. Parsing consumes the original
/// field order; the resulting data is evaluated by the native continuation engine.
class HotaScriptConverter
{
public:
	/// Reads a map text field and returns the stable identifier it was registered under.
	using LocalizeCallback = std::function<std::string(const TextIdentifier &)>;

	HotaScriptConverter(MapReaderH3M & reader, std::string mapName, LocalizeCallback localizeString);

	void readScript();


	/// Finalizes the native program after map-object identities have been resolved.
	void convert(CMap * map, const std::map<si32, ObjectInstanceID> & questIdentifierToId);

	/// Stable key of the native handler for an event bucket + id. Shared by the converter (which emits
	/// the handler table key) and the H3M loader (which tags objects/events with the handler to fire).
	std::string eventHandlerName(const std::string & bucket, int eventID);

private:
	MapReaderH3M & reader;
	std::string mapName;
	LocalizeCallback localizeString;

	// identify the event currently being emitted, used to generate text identifiers
	std::string currentBucket;
	int currentEventID = 0;
	int stringCounter = 0;

	// declarations collected while parsing, used to seed and persist the engine-side variables
	std::vector<ScriptVariableDefinition> variables;

	// H3M object identifiers referenced by object-identity predicates, resolved to names by the loader
	std::set<uint32_t> referencedObjects;

	JsonNode events;
	JsonNode variablesTable;

	void loadEventList(const std::string & bucket);
	void loadVariables();
	void loadEventMap();

	JsonNode loadActions();
	JsonNode loadAction(int code);
	void loadControlAction(int code, JsonNode & node);
	void loadRewardAction(int code, JsonNode & node);
	JsonNode loadCondition();
	JsonNode loadConditionInternal();
	JsonNode loadExpression();
	JsonNode loadExpressionInternal();

	/// Error for a field whose meaning is not known yet, naming the map and the event being converted
	std::runtime_error unsupported(const std::string & message) const;

	JsonNode loadImageList(int count);
	std::string localizedText(const std::string & role);

	JsonNode loadQuestReferences(CMap * map, const std::map<si32, ObjectInstanceID> & questIdentifierToId);

	/// Records an H3M object identifier and returns its native program lookup key.
	std::string questObjectRef(uint32_t identifier);
};
