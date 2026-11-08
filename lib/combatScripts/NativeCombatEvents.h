/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include <memory>
#include <string>
class ICombatEventScript;
namespace nativeEvents
{
/// Fixed shipped event implementations. Unknown source paths return null.
DLL_LINKAGE std::shared_ptr<ICombatEventScript> create(const std::string & sourcePath);
}
