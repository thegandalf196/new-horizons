/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "ScriptService.h"
#include <map>

/// Keeps the existing content IDs, parameter schemas and event priorities while
/// resolving shipped behavior to compiled C++ implementations during loading.
class DLL_LINKAGE NativeScriptFactory final : public IScriptFactory
{
	std::map<std::string, ScriptTypeDescription> descriptions;
public:
	void initialize(const ScriptTypeDescription & description) override;
	std::shared_ptr<ICombatEventScript> createCombatEventScript(const std::string & scriptId) const override;
	std::shared_ptr<IDamageCalculatorScript> createDamageCalculatorScript(const std::string & scriptId) const override;
	std::shared_ptr<spells::effects::Effect> createSpellEffect(const std::string & scriptId) const override;
};
