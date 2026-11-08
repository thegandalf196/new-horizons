/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "NativeModule.h"
#include "NativeMapEventDispatcher.h"
#include "NativeScriptFactory.h"

namespace scripting
{
namespace
{
// Retain the battle-state ownership interface during migration. Native rules
// receive their callback directly and never ask for a scripting context.
class NativePool final : public Pool
{
public:
	std::shared_ptr<Context> getContext(const Script *) const override
	{
		throw std::logic_error("Compiled New Horizons rules do not have language contexts");
	}
};
}

void NativeModule::installScripting(ScriptService & scripts)
{
	scripts.registerFactory(std::make_shared<NativeScriptFactory>());
}

std::unique_ptr<Pool> NativeModule::createPoolInstance(const Environment *) const
{
	return std::make_unique<NativePool>();
}

std::unique_ptr<MapEventDispatcher> NativeModule::createMapScriptDispatcher(CGameState & state, bool runInit) const
{
	return createNativeMapEventDispatcher(state, runInit);
}

void NativeModule::exportDocs(const boost::filesystem::path &) const
{
	throw std::logic_error("Language API export is unavailable for compiled New Horizons rules");
}
}
