/*
 * LuaImplementationIdentityTest.cpp, part of VCMI engine
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
#include <array>

namespace
{
using Identity = scripting::LuaImplementationIdentity;
using Implementation = spells::effects::ForecastImplementation;

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

void completeDamage(Identity & identity)
{
	load(identity, "spells/unitEffect", source("spells/unitEffect"));
	load(identity, "battleLog", source("battleLog"));
}

class EmptyEnvironment final : public Environment
{
public:
	const Services * services() const override { return nullptr; }
	const GameCb * game() const override { return nullptr; }
	const BattleCb * battle(const BattleID &) const override { return nullptr; }
};

using Patches = std::vector<std::pair<std::string, std::string>>;

Patches timedPatches()
{
	return {{"core", "spells/patches/timedShield"}, {"core", "spells/patches/timedBind"}};
}

std::unique_ptr<scripting::LuaScriptInstance> timedScript(scripting::LuaModule & host, const Patches & patches)
{
	const auto path = ScriptPath::builtinTODO("spells/timed").addPrefix("SCRIPTS/");
	return std::make_unique<scripting::LuaScriptInstance>(host, "core", path, patches);
}

Implementation attestTimed(const scripting::LuaScriptInstance & script)
{
	auto identity = Identity::fromScript(script);
	load(identity, "spells/unitEffect", source("spells/unitEffect"));
	identity.finish(true);
	return identity.get();
}
}

TEST(LuaImplementationIdentity, RequiresCompleteDamageClosureAndSuccessfulInitialization)
{
	Identity identity(source("spells/damage"), false);
	EXPECT_EQ(identity.get(), Implementation::UNKNOWN);
	completeDamage(identity);
	EXPECT_EQ(identity.get(), Implementation::UNKNOWN);
	identity.finish(true);
	EXPECT_EQ(identity.get(), Implementation::CANONICAL_DAMAGE);
}

TEST(LuaImplementationIdentity, RecognizesTimedWithoutBattleLog)
{
	Identity identity(source("spells/timed"), false);
	load(identity, "spells/unitEffect", source("spells/unitEffect"));
	identity.finish(true);
	EXPECT_EQ(identity.get(), Implementation::CANONICAL_TIMED);
}

TEST(LuaImplementationIdentity, RootWhitespaceOrCustomCodeFailsClosed)
{
	for(const auto & text : {source("spells/damage") + "\n", std::string("return { type = 'damage' }")})
	{
		Identity identity(text, false);
		completeDamage(identity);
		identity.finish(true);
		EXPECT_EQ(identity.get(), Implementation::UNKNOWN);
	}
}

TEST(LuaImplementationIdentity, DeclaredPatchesRejectEvenWhenNoneLoadedOrAttemptSucceeded)
{
	Identity identity(source("spells/damage"), true);
	completeDamage(identity);
	identity.finish(true);
	EXPECT_EQ(identity.get(), Implementation::UNKNOWN);
}

TEST(LuaImplementationIdentity, MissingOrFailedRootRejects)
{
	Identity missing("", false);
	missing.finish(true);
	EXPECT_EQ(missing.get(), Implementation::UNKNOWN);
	Identity failed(source("spells/damage"), false);
	completeDamage(failed);
	failed.finish(false);
	EXPECT_EQ(failed.get(), Implementation::UNKNOWN);
}

TEST(LuaImplementationIdentity, BothActualImportedSourcesMustMatchExactly)
{
	for(const auto & changed : {"spells/unitEffect", "battleLog"})
	{
		Identity identity(source("spells/damage"), false);
		for(const auto & name : {"spells/unitEffect", "battleLog"})
			load(identity, name, source(name) + (std::string(name) == changed ? "\n" : ""));
		identity.finish(true);
		EXPECT_EQ(identity.get(), Implementation::UNKNOWN) << changed;
	}
}

TEST(LuaImplementationIdentity, MissingImportAndFailedImportReject)
{
	Identity missing(source("spells/damage"), false);
	load(missing, "spells/unitEffect", source("spells/unitEffect"));
	missing.moduleRequested("battleLog");
	missing.finish(true);
	EXPECT_EQ(missing.get(), Implementation::UNKNOWN);

	Identity failed(source("spells/damage"), false);
	completeDamage(failed);
	failed.executionFailed();
	failed.finish(true);
	EXPECT_EQ(failed.get(), Implementation::UNKNOWN);
}

TEST(LuaImplementationIdentity, UnknownScopedAndInvalidImportAttemptsReject)
{
	for(const auto & name : {"unexpected", "core:spells/unitEffect", ""})
	{
		Identity identity(source("spells/damage"), false);
		identity.moduleRequested(name);
		completeDamage(identity);
		identity.finish(true);
		EXPECT_EQ(identity.get(), Implementation::UNKNOWN) << name;
	}
}

TEST(LuaImplementationIdentity, DuplicateOrUnrequestedImportsReject)
{
	Identity duplicate(source("spells/damage"), false);
	completeDamage(duplicate);
	load(duplicate, "battleLog", source("battleLog"));
	duplicate.finish(true);
	EXPECT_EQ(duplicate.get(), Implementation::UNKNOWN);

	Identity unrequested(source("spells/damage"), false);
	unrequested.moduleLoaded("battleLog", source("battleLog"));
	completeDamage(unrequested);
	unrequested.finish(true);
	EXPECT_EQ(unrequested.get(), Implementation::UNKNOWN);
}

TEST(LuaImplementationIdentity, AnyLaterImportAttemptPermanentlyInvalidates)
{
	for(const auto & name : {"battleLog", "unexpected", ""})
	{
		Identity identity(source("spells/damage"), false);
		completeDamage(identity);
		identity.finish(true);
		ASSERT_EQ(identity.get(), Implementation::CANONICAL_DAMAGE);
		identity.moduleRequested(name);
		EXPECT_EQ(identity.get(), Implementation::UNKNOWN) << name;
		identity.finish(true);
		EXPECT_EQ(identity.get(), Implementation::UNKNOWN);
	}
}

TEST(LuaImplementationIdentity, ContextAttestsActualLoadedImportClosure)
{
	scripting::LuaModule host;
	EmptyEnvironment environment;
	for(const auto & name : {"spells/damage", "spells/timed"})
	{
		scripting::LuaScriptInstance script(host, "core", source(name));
		scripting::LuaContext context(&script, &environment);
		EXPECT_EQ(context.forecastImplementation(), Implementation::UNKNOWN);
		context.initialize();
		const auto expected = std::string(name) == "spells/damage"
			? Implementation::CANONICAL_DAMAGE : Implementation::CANONICAL_TIMED;
		EXPECT_EQ(context.forecastImplementation(), expected);
	}
}

TEST(LuaImplementationIdentity, MissingDeclaredPatchStillRejectsActualContext)
{
	scripting::LuaModule host;
	EmptyEnvironment environment;
	scripting::LuaScriptInstance script(host, "core", source("spells/damage"),
		{"forecastIdentityDeliberatelyMissingPatch"});
	ASSERT_TRUE(script.hasPatchDeclarations());
	ASSERT_EQ(script.layers.size(), 1);
	scripting::LuaContext context(&script, &environment);
	context.initialize();
	EXPECT_EQ(context.forecastImplementation(), Implementation::UNKNOWN);
}

TEST(LuaImplementationIdentity, ContextUsesActualSourceAtInitialization)
{
	scripting::LuaModule host;
	EmptyEnvironment environment;
	scripting::LuaScriptInstance script(host, "core", source("spells/damage"));
	scripting::LuaContext context(&script, &environment);
	script.layers.front().sourceText += "\n";
	context.initialize();
	EXPECT_EQ(context.forecastImplementation(), Implementation::UNKNOWN);
}

TEST(LuaImplementationIdentity, ReinitializingContextCannotRestoreAttestation)
{
	scripting::LuaModule host;
	EmptyEnvironment environment;
	scripting::LuaScriptInstance script(host, "core", source("spells/timed"));
	scripting::LuaContext context(&script, &environment);
	context.initialize();
	ASSERT_EQ(context.forecastImplementation(), Implementation::CANONICAL_TIMED);
	context.initialize();
	EXPECT_EQ(context.forecastImplementation(), Implementation::UNKNOWN);
}

TEST(LuaImplementationIdentity, ExtraActualLayersRejectWithoutPatchMetadata)
{
	scripting::LuaModule host;
	EmptyEnvironment environment;
	scripting::LuaScriptInstance script(host, "core", source("spells/timed"));
	script.layers.push_back({"return Base", "test:extraLayer"});
	scripting::LuaContext context(&script, &environment);
	context.initialize();
	EXPECT_EQ(context.forecastImplementation(), Implementation::UNKNOWN);
}

TEST(LuaImplementationIdentity, RuntimeMissingMethodInvalidatesActualContext)
{
	scripting::LuaModule host;
	EmptyEnvironment environment;
	scripting::LuaScriptInstance script(host, "core", source("spells/timed"));
	scripting::LuaContext context(&script, &environment);
	context.initialize();
	ASSERT_EQ(context.forecastImplementation(), Implementation::CANONICAL_TIMED);
	context.callFunction<void>("forecastIdentityDeliberatelyMissingMethod");
	EXPECT_EQ(context.forecastImplementation(), Implementation::UNKNOWN);
}

TEST(LuaImplementationIdentity, RuntimeLuaErrorInvalidatesActualContext)
{
	scripting::LuaModule host;
	EmptyEnvironment environment;
	scripting::LuaScriptInstance script(host, "core", source("spells/timed"));
	scripting::LuaContext context(&script, &environment);
	context.initialize();
	ASSERT_EQ(context.forecastImplementation(), Implementation::CANONICAL_TIMED);
	// The canonical method requires self/mechanics/target; no arguments must fail.
	context.callFunction<void>("transformTarget");
	EXPECT_EQ(context.forecastImplementation(), Implementation::UNKNOWN);
}

TEST(LuaImplementationIdentity, RuntimeReturnConversionErrorInvalidatesActualContext)
{
	scripting::LuaModule host;
	EmptyEnvironment environment;
	scripting::LuaScriptInstance script(host, "core", source("spells/timed"));
	scripting::LuaContext context(&script, &environment);
	context.initialize();
	ASSERT_EQ(context.forecastImplementation(), Implementation::CANONICAL_TIMED);
	// Inherited neutral preview returns a table, never the requested integer.
	EXPECT_EQ(context.callFunction<int>("getHealthChange"), 0);
	EXPECT_EQ(context.forecastImplementation(), Implementation::UNKNOWN);
}

TEST(LuaImplementationIdentity, ShippedTimedChainAttestsActualThreeLayerContext)
{
	scripting::LuaModule host;
	EmptyEnvironment environment;
	const auto script = timedScript(host, timedPatches());
	ASSERT_EQ(script->layers.size(), 3);
	ASSERT_EQ(script->getPatchDeclarations().size(), 2);
	EXPECT_EQ(attestTimed(*script), Implementation::CANONICAL_TIMED);
	scripting::LuaContext context(script.get(), &environment);
	EXPECT_EQ(context.forecastImplementation(), Implementation::UNKNOWN);
	context.initialize();
	EXPECT_EQ(context.forecastImplementation(), Implementation::CANONICAL_TIMED);
}

TEST(LuaImplementationIdentity, ShippedTimedChainRejectsEveryChangedMissingExtraOrReorderedLayer)
{
	scripting::LuaModule host;
	for(size_t index = 0; index < 3; ++index)
	{
		auto script = timedScript(host, timedPatches());
		script->layers[index].sourceText += "\n";
		EXPECT_EQ(attestTimed(*script), Implementation::UNKNOWN) << index;
	}
	auto missing = timedScript(host, timedPatches());
	missing->layers.pop_back();
	EXPECT_EQ(attestTimed(*missing), Implementation::UNKNOWN);
	auto extra = timedScript(host, timedPatches());
	extra->layers.push_back(extra->layers.back());
	EXPECT_EQ(attestTimed(*extra), Implementation::UNKNOWN);
	auto reordered = timedScript(host, timedPatches());
	std::swap(reordered->layers[1], reordered->layers[2]);
	EXPECT_EQ(attestTimed(*reordered), Implementation::UNKNOWN);
}

TEST(LuaImplementationIdentity, ShippedTimedChainRequiresExactDeclaredCountOrderScopeAndPaths)
{
	scripting::LuaModule host;
	const auto canonical = timedScript(host, timedPatches());
	for(int change = 0; change < 5; ++change)
	{
		auto patches = timedPatches();
		if(change == 0)
			patches.pop_back();
		if(change == 1)
			patches.emplace_back("core", "forecastIdentityDeliberatelyMissingPatch");
		if(change == 2)
			std::swap(patches[0], patches[1]);
		if(change == 3)
			patches[0].first = "root";
		if(change == 4)
			patches[0].second = "spells/patches/timedBind";
		auto script = timedScript(host, patches);
		// Isolate declaration checks even when effective loaded layers look exact.
		script->layers = canonical->layers;
		EXPECT_EQ(attestTimed(*script), Implementation::UNKNOWN) << change;
	}
}

TEST(LuaImplementationIdentity, ShippedTimedChainRequiresActualImportAndSuccessfulExecution)
{
	scripting::LuaModule host;
	const auto script = timedScript(host, timedPatches());
	auto missing = Identity::fromScript(*script);
	missing.moduleRequested("spells/unitEffect");
	missing.finish(true);
	EXPECT_EQ(missing.get(), Implementation::UNKNOWN);
	auto changed = Identity::fromScript(*script);
	load(changed, "spells/unitEffect", source("spells/unitEffect") + "\n");
	changed.finish(true);
	EXPECT_EQ(changed.get(), Implementation::UNKNOWN);
	auto failed = Identity::fromScript(*script);
	load(failed, "spells/unitEffect", source("spells/unitEffect"));
	failed.executionFailed();
	failed.finish(true);
	EXPECT_EQ(failed.get(), Implementation::UNKNOWN);
}

TEST(LuaImplementationIdentity, ShippedTimedChainRejectsUnexpectedLaterImportsAndRuntimeFailures)
{
	scripting::LuaModule host;
	EmptyEnvironment environment;
	const auto script = timedScript(host, timedPatches());
	auto unknown = Identity::fromScript(*script);
	unknown.moduleRequested("unknown");
	load(unknown, "spells/unitEffect", source("spells/unitEffect"));
	unknown.finish(true);
	EXPECT_EQ(unknown.get(), Implementation::UNKNOWN);
	auto later = Identity::fromScript(*script);
	load(later, "spells/unitEffect", source("spells/unitEffect"));
	later.finish(true);
	ASSERT_EQ(later.get(), Implementation::CANONICAL_TIMED);
	later.moduleRequested("spells/unitEffect");
	EXPECT_EQ(later.get(), Implementation::UNKNOWN);
	scripting::LuaContext context(script.get(), &environment);
	context.initialize();
	ASSERT_EQ(context.forecastImplementation(), Implementation::CANONICAL_TIMED);
	context.callFunction<void>("forecastIdentityDeliberatelyMissingMethod");
	EXPECT_EQ(context.forecastImplementation(), Implementation::UNKNOWN);
}

namespace
{
constexpr std::array<const char *, 5> lifeDrainModules = {
	"spells/unitEffect", "spells/damage", "spells/unitEffect", "battleLog", "battleLog"};
void completeLifeDrain(Identity & identity)
{
	for(const auto * name : lifeDrainModules)
		load(identity, name, source(name));
}
}
TEST(LuaImplementationIdentity, LifeDrainRequiresItsExactTransitiveClosure)
{
	Identity identity(source("spells/lifeDrain"), false);
	completeLifeDrain(identity);
	EXPECT_EQ(identity.get(), Implementation::UNKNOWN);
	identity.finish(true);
	EXPECT_EQ(identity.get(), Implementation::CANONICAL_LIFE_DRAIN);
}
TEST(LuaImplementationIdentity, LifeDrainEveryModifiedDependencyAndRootFailClosed)
{
	for(size_t changed = 0; changed < lifeDrainModules.size(); ++changed)
	{
		Identity identity(source("spells/lifeDrain"), false);
		for(size_t index = 0; index < lifeDrainModules.size(); ++index)
		{
			const auto * name = lifeDrainModules[index];
			load(identity, name, source(name) + (index == changed ? "\n-- changed" : ""));
		}
		identity.finish(true);
		EXPECT_EQ(identity.get(), Implementation::UNKNOWN);
	}
	Identity root(source("spells/lifeDrain") + "\n-- changed", false);
	completeLifeDrain(root);
	root.finish(true);
	EXPECT_EQ(root.get(), Implementation::UNKNOWN);
}
TEST(LuaImplementationIdentity, LifeDrainMissingDuplicateUnknownPatchOrFailedExecutionRejects)
{
	for(size_t missing = 0; missing < lifeDrainModules.size(); ++missing)
	{
		Identity identity(source("spells/lifeDrain"), false);
		for(size_t index = 0; index < lifeDrainModules.size(); ++index)
			if(index != missing)
				load(identity, lifeDrainModules[index], source(lifeDrainModules[index]));
		identity.finish(true);
		EXPECT_EQ(identity.get(), Implementation::UNKNOWN);
	}
	Identity patched(source("spells/lifeDrain"), true);
	completeLifeDrain(patched);
	patched.finish(true);
	EXPECT_EQ(patched.get(), Implementation::UNKNOWN);
	Identity unknown(source("spells/lifeDrain"), false);
	completeLifeDrain(unknown);
	load(unknown, "unknown", "return {}");
	unknown.finish(true);
	EXPECT_EQ(unknown.get(), Implementation::UNKNOWN);
	Identity failed(source("spells/lifeDrain"), false);
	completeLifeDrain(failed);
	failed.executionFailed();
	failed.finish(true);
	EXPECT_EQ(failed.get(), Implementation::UNKNOWN);
}
TEST(LuaImplementationIdentity, LifeDrainReorderedOrPostInitializationImportRejects)
{
	Identity reordered(source("spells/lifeDrain"), false);
	load(reordered, "spells/damage", source("spells/damage"));
	completeLifeDrain(reordered);
	reordered.finish(true);
	EXPECT_EQ(reordered.get(), Implementation::UNKNOWN);
	Identity later(source("spells/lifeDrain"), false);
	completeLifeDrain(later);
	later.finish(true);
	ASSERT_EQ(later.get(), Implementation::CANONICAL_LIFE_DRAIN);
	later.moduleRequested("battleLog");
	EXPECT_EQ(later.get(), Implementation::UNKNOWN);
}
TEST(LuaImplementationIdentity, LifeDrainActualContextAttestsAndLaterRuntimeErrorInvalidates)
{
	scripting::LuaModule host;
	EmptyEnvironment environment;
	scripting::LuaScriptInstance script(host, "core", source("spells/lifeDrain"));
	scripting::LuaContext context(&script, &environment);
	context.initialize();
	ASSERT_EQ(context.forecastImplementation(), Implementation::CANONICAL_LIFE_DRAIN);
	context.callFunction<void>("forecastIdentityDeliberatelyMissingMethod");
	EXPECT_EQ(context.forecastImplementation(), Implementation::UNKNOWN);
}
