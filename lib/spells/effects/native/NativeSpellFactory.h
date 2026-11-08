/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <memory>
struct ScriptTypeDescription;
namespace spells::effects { class Effect; }
namespace nativeSpells
{
/// Creates only an explicitly supported fixed effect and ordered patch chain.
/// Unknown registrations return null and must be reported by the content loader.
DLL_LINKAGE std::shared_ptr<spells::effects::Effect> create(const ScriptTypeDescription & description);
}
