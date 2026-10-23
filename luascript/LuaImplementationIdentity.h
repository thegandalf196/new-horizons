/*
 * LuaImplementationIdentity.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "../lib/spells/effects/Effect.h"
#include "../lib/combatScripts/ICombatEventScript.h"

#include <string_view>

namespace scripting
{
class LuaScriptInstance;

/// Attests the reviewed spell implementations/imports or the separate complete
/// calculator layer chain. Observes bytes already loaded by the interpreter;
/// never resolves a resource itself. Does not attest effect parameters.
class LuaImplementationIdentity
{
public:
	LuaImplementationIdentity(std::string_view rootSource = {}, bool hasPatchDeclarations = false);
	static LuaImplementationIdentity fromScript(const LuaScriptInstance & script);

	void moduleRequested(std::string_view name);
	void moduleLoaded(std::string_view name, std::string_view source);
	void executionFailed();
	void finish(bool initialized);

	spells::effects::ForecastImplementation get() const;
	bool hasCanonicalDamageCalculator() const;
	CombatEventForecastImplementation getCombatEventImplementation() const;

private:
	spells::effects::ForecastImplementation implementation;
	CombatEventForecastImplementation combatEventImplementation = CombatEventForecastImplementation::UNKNOWN;
	unsigned requested = 0;
	unsigned loaded = 0;
	unsigned lifeDrainRequested = 0;
	unsigned lifeDrainLoaded = 0;
	bool finished = false;
	bool canonicalDamageCalculator = false;

	unsigned expectedModules() const;
	void invalidate();
};

}
