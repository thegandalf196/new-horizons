/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "NativeMapEventDispatcher.h"
#include "nativeMap/EventRuntime.h"
#include "nativeMap/LegacyHotaImport.h"
#include "../gameState/CGameState.h"
#include "../mapping/CMap.h"

std::unique_ptr<scripting::MapEventDispatcher> scripting::createNativeMapEventDispatcher(CGameState & state, bool runInit)
{
	const auto & source = state.getMap().scriptSource;
	if(source.empty()) return nullptr;
	const auto first = source.find_first_not_of(" \t\r\n");
	JsonParsingSettings settings;
	settings.strict = true;
	settings.maxDepth = 256;
	const bool nativeSource = first != std::string::npos && source[first] == '{';
	JsonNode program = nativeSource
		? JsonNode(source.data(), source.size(), settings, "native map events")
		: nativeMap::importLegacyHotaEvents(source);
	if(!program["format"].isString() || program["format"].String() != "new-horizons-native-events"
		|| !program["version"].isNumber() || program["version"].Integer() != 1)
		throw std::runtime_error("Unsupported native map event program format");
	const auto migratedSource = nativeSource ? std::string() : program.toCompactString();
	auto dispatcher = std::make_unique<nativeMap::Runtime>(state, std::move(program), runInit);
	if(!nativeSource) state.getMap().scriptSource = migratedSource;
	return dispatcher;
}
