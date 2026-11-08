/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include <vcmi/scripting/Service.h>

namespace scripting
{
/// Compiled New Horizons rules. Existing content/serialization interfaces retain
/// their IDs; no source compilation, virtual machine or language context exists.
class DLL_LINKAGE NativeModule final : public Service
{
public:
	void installScripting(ScriptService & scripts) override;
	std::unique_ptr<Pool> createPoolInstance(const Environment *) const override;
	std::unique_ptr<MapEventDispatcher> createMapScriptDispatcher(CGameState &, bool runInit) const override;
	void exportDocs(const boost::filesystem::path &) const override;
};
}
