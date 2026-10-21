/*
 * LuaCombatEventIdentityTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../../luascript/StdInc.h"
#include "../../luascript/LuaImplementationIdentity.h"
#include "../../luascript/LuaContext.h"
#include "../../luascript/LuaModule.h"
#include "../../luascript/LuaScriptInstance.h"
#include "../../lib/filesystem/Filesystem.h"

namespace
{
using Identity = scripting::LuaImplementationIdentity;
using Implementation = CombatEventForecastImplementation;

std::string source(const std::string & name)
{
	const auto path = ScriptPath::builtinTODO(name).addPrefix("SCRIPTS/");
	auto bytes = CResourceHandler::get()->load(path)->readAll();
	return std::string(reinterpret_cast<const char *>(bytes.first.get()), bytes.second);
}

void load(Identity & identity, const std::string & name, const std::string & text)
{
	identity.moduleRequested(name);
	identity.moduleLoaded(name, text);
}

void complete(Identity & identity, bool hexOfPain)
{
	load(identity, "combat/combatScript", source("combat/combatScript"));
	if(hexOfPain)
		load(identity, "battleLog", source("battleLog"));
}

class EmptyEnvironment final : public Environment
{
public:
	const Services * services() const override { return nullptr; }
	const GameCb * game() const override { return nullptr; }
	const BattleCb * battle(const BattleID &) const override { return nullptr; }
};
}

TEST(LuaCombatEventIdentity, FocusMagicAndHexOfPainRequireTheirCompleteActualClosure)
{
	for(bool hexOfPain : {false, true})
	{
		Identity identity(source(hexOfPain ? "combat/hexOfPain" : "combat/focusMagic"), false);
		complete(identity, hexOfPain);
		EXPECT_EQ(identity.getCombatEventImplementation(), Implementation::UNKNOWN);
		identity.finish(true);
		EXPECT_EQ(identity.getCombatEventImplementation(), hexOfPain
			? Implementation::CANONICAL_HEX_OF_PAIN : Implementation::CANONICAL_FOCUS_MAGIC);
		EXPECT_EQ(identity.get(), spells::effects::ForecastImplementation::UNKNOWN);
		EXPECT_FALSE(identity.hasCanonicalDamageCalculator());
	}
}

TEST(LuaCombatEventIdentity, ChangedOrPatchedRootsAndOtherCanonicalEventsStayUnknown)
{
	for(bool hexOfPain : {false, true})
	{
		const auto text = source(hexOfPain ? "combat/hexOfPain" : "combat/focusMagic");
		Identity changed(text + "\n", false);
		complete(changed, hexOfPain);
		changed.finish(true);
		EXPECT_EQ(changed.getCombatEventImplementation(), Implementation::UNKNOWN);
		Identity patched(text, true);
		complete(patched, hexOfPain);
		patched.finish(true);
		EXPECT_EQ(patched.getCombatEventImplementation(), Implementation::UNKNOWN);
	}
	Identity other(source("combat/arcaneBreach"), false);
	complete(other, false);
	other.finish(true);
	EXPECT_EQ(other.getCombatEventImplementation(), Implementation::UNKNOWN);
}

TEST(LuaCombatEventIdentity, ChangedTransitiveImportOrMissingBattleLogRejects)
{
	for(const auto & changed : {"combat/combatScript", "battleLog"})
	{
		Identity identity(source("combat/hexOfPain"), false);
		for(const auto & name : {"combat/combatScript", "battleLog"})
			load(identity, name, source(name) + (std::string(name) == changed ? "\n" : ""));
		identity.finish(true);
		EXPECT_EQ(identity.getCombatEventImplementation(), Implementation::UNKNOWN) << changed;
	}
	Identity missing(source("combat/hexOfPain"), false);
	complete(missing, false);
	missing.finish(true);
	EXPECT_EQ(missing.getCombatEventImplementation(), Implementation::UNKNOWN);
}

TEST(LuaCombatEventIdentity, UnexpectedOrLaterRequiresAndExecutionFailuresReject)
{
	for(const auto & name : {"spells/unitEffect", "unknown", "core:combat/combatScript", ""})
	{
		Identity identity(source("combat/focusMagic"), false);
		identity.moduleRequested(name);
		complete(identity, false);
		identity.finish(true);
		EXPECT_EQ(identity.getCombatEventImplementation(), Implementation::UNKNOWN) << name;
	}
	Identity late(source("combat/hexOfPain"), false);
	complete(late, true);
	late.finish(true);
	ASSERT_EQ(late.getCombatEventImplementation(), Implementation::CANONICAL_HEX_OF_PAIN);
	late.moduleRequested("battleLog");
	EXPECT_EQ(late.getCombatEventImplementation(), Implementation::UNKNOWN);
	Identity failed(source("combat/focusMagic"), false);
	complete(failed, false);
	failed.executionFailed();
	failed.finish(true);
	EXPECT_EQ(failed.getCombatEventImplementation(), Implementation::UNKNOWN);
}

TEST(LuaCombatEventIdentity, ActualContextsAttestImportsAndInvalidateOnRuntimeFailure)
{
	scripting::LuaModule host;
	EmptyEnvironment environment;
	for(bool hexOfPain : {false, true})
	{
		scripting::LuaScriptInstance script(host, "core", source(hexOfPain ? "combat/hexOfPain" : "combat/focusMagic"));
		scripting::LuaContext context(&script, &environment);
		context.initialize();
		ASSERT_EQ(context.combatEventForecastImplementation(), hexOfPain
			? Implementation::CANONICAL_HEX_OF_PAIN : Implementation::CANONICAL_FOCUS_MAGIC);
		context.callFunction<void>("forecastIdentityDeliberatelyMissingMethod");
		EXPECT_EQ(context.combatEventForecastImplementation(), Implementation::UNKNOWN);
	}
}
