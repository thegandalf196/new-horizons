/*
 * LuaImplementationIdentityTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../../luascript/StdInc.h"
#include "../../luascript/LuaImplementationIdentity.h"
#include "HistoricalTimedScript.h"
#include "HistoricalLifeDrainScript.h"
#include "../../luascript/LuaContext.h"
#include "../../luascript/LuaModule.h"
#include "../../luascript/LuaScriptInstance.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/filesystem/CFilesystemLoader.h"
#include "../../lib/VCMIDirs.h"
#include <boost/filesystem/fstream.hpp>
#include <array>
#include <stdexcept>

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

std::string historicalSorcerySource()
{
	auto text = source("spells/sorceryRevision");
	const std::string current = "bonus.val = mechanics:scaleSlowVariantValue(bonus.val, powerPercent)";
	const auto position = text.find(current);
	if(position == std::string::npos || text.find(current, position + current.size()) != std::string::npos)
		throw std::runtime_error("Historical Sorcery fixture requires the exact single shared-arithmetic call");
	text.replace(position, current.size(), "bonus.val = math.ceil(bonus.val * powerPercent / 100)");
	return text;
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

void completeTimed(Identity & identity)
{
	load(identity, "spells/unitEffect", source("spells/unitEffect"));
	load(identity, "spells/sorceryRevision", source("spells/sorceryRevision"));
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
	completeTimed(identity);
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
	completeTimed(identity);
	identity.finish(true);
	EXPECT_EQ(identity.get(), Implementation::CANONICAL_TIMED);
}

TEST(LuaImplementationIdentity, TimedRequiresBothReviewedImportsWithExactSourceBytes)
{
	for(const auto & changed : {"spells/unitEffect", "spells/sorceryRevision"})
	{
		Identity modified(source("spells/timed"), false);
		Identity omitted(source("spells/timed"), false);
		Identity failedLoad(source("spells/timed"), false);
		for(const auto & name : {"spells/unitEffect", "spells/sorceryRevision"})
		{
			load(modified, name, source(name) + (std::string(name) == changed ? "\n" : ""));
			if(std::string(name) != changed)
			{
				load(omitted, name, source(name));
				load(failedLoad, name, source(name));
			}
			else
				failedLoad.moduleRequested(name);
		}
		modified.finish(true);
		omitted.finish(true);
		failedLoad.finish(true);
		EXPECT_EQ(modified.get(), Implementation::UNKNOWN) << changed;
		EXPECT_EQ(omitted.get(), Implementation::UNKNOWN) << changed;
		EXPECT_EQ(failedLoad.get(), Implementation::UNKNOWN) << changed;
	}
}

TEST(LuaImplementationIdentity, SorceryImportDoesNotBroadenOtherOrTimedClosures)
{
	Identity damage(source("spells/damage"), false);
	completeDamage(damage);
	load(damage, "spells/sorceryRevision", source("spells/sorceryRevision"));
	damage.finish(true);
	EXPECT_EQ(damage.get(), Implementation::UNKNOWN);

	Identity duplicate(source("spells/timed"), false);
	completeTimed(duplicate);
	load(duplicate, "spells/sorceryRevision", source("spells/sorceryRevision"));
	duplicate.finish(true);
	EXPECT_EQ(duplicate.get(), Implementation::UNKNOWN);

	Identity unrequested(source("spells/timed"), false);
	unrequested.moduleLoaded("spells/sorceryRevision", source("spells/sorceryRevision"));
	completeTimed(unrequested);
	unrequested.finish(true);
	EXPECT_EQ(unrequested.get(), Implementation::UNKNOWN);
}

TEST(LuaImplementationIdentity, RootWhitespaceOrCustomCodeFailsClosed)
{
	for(const auto & name : {"spells/damage", "spells/timed"})
	{
		Identity identity(source(name) + "\n", false);
		if(std::string_view(name) == "spells/damage")
			completeDamage(identity);
		else
			completeTimed(identity);
		identity.finish(true);
		EXPECT_EQ(identity.get(), Implementation::UNKNOWN) << name;
	}
	Identity custom("return { type = 'damage' }", false);
	completeDamage(custom);
	custom.finish(true);
	EXPECT_EQ(custom.get(), Implementation::UNKNOWN);
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
	load(changed, "spells/sorceryRevision", source("spells/sorceryRevision"));
	changed.finish(true);
	EXPECT_EQ(changed.get(), Implementation::UNKNOWN);
	auto failed = Identity::fromScript(*script);
	completeTimed(failed);
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
	completeTimed(unknown);
	unknown.finish(true);
	EXPECT_EQ(unknown.get(), Implementation::UNKNOWN);
	auto later = Identity::fromScript(*script);
	completeTimed(later);
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
void completeLifeDrain(Identity & identity, bool current = true)
{
	for(const auto * name : lifeDrainModules)
		load(identity, name, !current && std::string_view(name) == "spells/damage"
			? std::string(historicalLifeDrainIdentityTest::DAMAGE_SOURCE) : source(name));
}

// Serial test-only overlay: exercise the bytes actually imported by LuaContext.
// Keep it in the isolated test cache and restore the filesystem before returning.
class HistoricalLifeDrainDamageScope
{
	boost::filesystem::path directory;
	std::string identifier;
public:
	explicit HistoricalLifeDrainDamageScope(bool historical)
	{
		if(!historical)
			return;
		identifier = boost::filesystem::unique_path("life-drain-%%%%-%%%%-%%%%").string();
		directory = VCMIDirs::get().userCachePath() / identifier;
		try
		{
			boost::filesystem::create_directories(directory / "spells");
			boost::filesystem::ofstream output(directory / "spells/damage.lua", std::ios::binary);
			output << historicalLifeDrainIdentityTest::DAMAGE_SOURCE;
			output.close();
			if(!output)
				throw std::runtime_error("Failed to write historical Life Drain dependency");
			CResourceHandler::addFilesystem("root", identifier,
				std::make_unique<CFilesystemLoader>("SCRIPTS/", directory));
			if(source("spells/damage") != historicalLifeDrainIdentityTest::DAMAGE_SOURCE)
				throw std::runtime_error("Historical Life Drain import was not selected");
		}
		catch(...)
		{
			CResourceHandler::removeFilesystem("root", identifier);
			boost::filesystem::remove_all(directory);
			throw;
		}
	}
	~HistoricalLifeDrainDamageScope()
	{
		if(!identifier.empty())
		{
			CResourceHandler::removeFilesystem("root", identifier);
			boost::filesystem::remove_all(directory);
		}
	}
};
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

TEST(LuaImplementationIdentity, SharedArithmeticTimedRejectsHistoricalSorceryImport)
{
	const auto historicalSorcery = historicalSorcerySource();
	scripting::LuaModule host;
	const auto script = timedScript(host, timedPatches());
	for(const bool layered : {false, true})
	{
		auto identity = layered ? Identity::fromScript(*script) : Identity(source("spells/timed"), false);
		load(identity, "spells/unitEffect", source("spells/unitEffect"));
		load(identity, "spells/sorceryRevision", historicalSorcery);
		identity.finish(true);
		EXPECT_EQ(identity.get(), Implementation::UNKNOWN) << layered;
	}
}

TEST(LuaImplementationIdentity, HistoricalTimedKeepsExactHistoricalImportAndRejectsCurrentImport)
{
	scripting::LuaModule host;
	const auto script = timedScript(host, timedPatches());
	script->layers.front().sourceText = std::string(historicalTimedIdentityTest::TIMED_SOURCE);
	for(const bool layered : {false, true})
		for(const bool historicalImport : {true, false})
		{
			auto identity = layered ? Identity::fromScript(*script)
				: Identity(historicalTimedIdentityTest::TIMED_SOURCE, false);
			load(identity, "spells/unitEffect", source("spells/unitEffect"));
			load(identity, "spells/sorceryRevision", historicalImport
				? historicalSorcerySource() : source("spells/sorceryRevision"));
			identity.finish(true);
			EXPECT_EQ(identity.get(), historicalImport ? Implementation::CANONICAL_TIMED : Implementation::UNKNOWN)
				<< "layered=" << layered << " historicalImport=" << historicalImport;
		}
}

TEST(LuaImplementationIdentity, HistoricalAndSharedLifeDrainRootsRequireExactBytesAndClosure)
{
	scripting::LuaModule host;
	for(const auto & original : {std::string(historicalLifeDrainIdentityTest::SOURCE),
		std::string(historicalLifeDrainIdentityTest::SHARED_SOURCE), source("spells/lifeDrain")})
		for(const bool changed : {false, true})
			for(const bool fromScript : {false, true})
			{
				const auto text = original + (changed ? "\n-- unreviewed change" : "");
				scripting::LuaScriptInstance script(host, "core", text);
				auto identity = fromScript ? Identity::fromScript(script) : Identity(text, false);
				completeLifeDrain(identity, original == source("spells/lifeDrain"));
				identity.finish(true);
				EXPECT_EQ(identity.get(), changed ? Implementation::UNKNOWN : Implementation::CANONICAL_LIFE_DRAIN)
					<< "changed=" << changed << " fromScript=" << fromScript;
			}
}

TEST(LuaImplementationIdentity, HistoricalAndSharedLifeDrainContextsAttestActualImportsAndRejectChanges)
{
	scripting::LuaModule host;
	EmptyEnvironment environment;
	for(const auto & original : {std::string(historicalLifeDrainIdentityTest::SOURCE),
		std::string(historicalLifeDrainIdentityTest::SHARED_SOURCE), source("spells/lifeDrain")})
		for(const bool changed : {false, true})
		{
			HistoricalLifeDrainDamageScope imports(original != source("spells/lifeDrain"));
			scripting::LuaScriptInstance script(host, "core", original + (changed ? "\n-- unreviewed change" : ""));
			scripting::LuaContext context(&script, &environment);
			context.initialize();
			EXPECT_EQ(context.forecastImplementation(), changed ? Implementation::UNKNOWN : Implementation::CANONICAL_LIFE_DRAIN)
				<< "changed=" << changed;
		}
}

TEST(LuaImplementationIdentity, LifeDrainCurrentAndHistoricalDamageImportsNeverMix)
{
	scripting::LuaModule host;
	EmptyEnvironment environment;
	const auto currentRoot = source("spells/lifeDrain");
	for(const auto & root : {std::string(historicalLifeDrainIdentityTest::SOURCE),
		std::string(historicalLifeDrainIdentityTest::SHARED_SOURCE), currentRoot})
		for(const bool currentImport : {false, true})
		{
			const auto expected = (root == currentRoot) == currentImport
				? Implementation::CANONICAL_LIFE_DRAIN : Implementation::UNKNOWN;
			Identity identity(root, false);
			completeLifeDrain(identity, currentImport);
			identity.finish(true);
			EXPECT_EQ(identity.get(), expected);
			HistoricalLifeDrainDamageScope imports(!currentImport);
			scripting::LuaScriptInstance script(host, "core", root);
			scripting::LuaContext context(&script, &environment);
			context.initialize();
			EXPECT_EQ(context.forecastImplementation(), expected);
		}
}

TEST(LuaImplementationIdentity, LifeDrainHistoricalImportScopeRestoresCurrentSource)
{
	const auto current = source("spells/damage");
	{
		HistoricalLifeDrainDamageScope imports(true);
		EXPECT_EQ(source("spells/damage"), historicalLifeDrainIdentityTest::DAMAGE_SOURCE);
	}
	EXPECT_EQ(source("spells/damage"), current);
}
