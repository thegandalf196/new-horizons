/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include <vcmi/scripting/MapEventDispatcher.h>
#include <memory>

class CGameState;
namespace scripting
{
/// Returns no dispatcher for an empty map program. Nonempty unsupported source
/// is an explicit load error; it is never silently ignored.
DLL_LINKAGE std::unique_ptr<MapEventDispatcher> createNativeMapEventDispatcher(CGameState & state, bool runInit);
}
