/*
 * LuaDamageCalculatorIdentityTest.cpp, part of VCMI engine
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

namespace
{
using Patches = std::vector<std::pair<std::string, std::string>>;
using Identity = scripting::LuaImplementationIdentity;

Patches canonicalPatches()
{
	Patches result;
	for(const auto & path : {"damage/siegeWeapon", "damage/magicElemental", "damage/psychicElemental",
		"damage/vulnerableFromBack", "damage/hatesTrait", "damage/revenge",
		"damage/enemyAttackReduction", "damage/damageReceivedCap"})
		result.emplace_back("core", path);
	return result;
}

std::unique_ptr<scripting::LuaScriptInstance> calculator(scripting::LuaModule & host, const Patches & patches)
{
	const auto path = ScriptPath::builtinTODO("damage/damageCalculator").addPrefix("SCRIPTS/");
	return std::make_unique<scripting::LuaScriptInstance>(host, "core", path, patches);
}

bool attests(const scripting::LuaScriptInstance & script)
{
	auto identity = Identity::fromScript(script);
	identity.finish(true);
	return identity.hasCanonicalDamageCalculator();
}

class EmptyEnvironment final : public Environment
{
public:
	const Services * services() const override { return nullptr; }
	const GameCb * game() const override { return nullptr; }
	const BattleCb * battle(const BattleID &) const override { return nullptr; }
};
}

TEST(LuaDamageCalculatorIdentity, CompleteOrderedNineLayerChainRequiresSuccessfulInitialization)
{
	scripting::LuaModule host;
	const auto script = calculator(host, canonicalPatches());
	ASSERT_EQ(script->layers.size(), 9);
	ASSERT_EQ(script->getPatchDeclarations().size(), 8);
	auto identity = Identity::fromScript(*script);
	EXPECT_FALSE(identity.hasCanonicalDamageCalculator());
	identity.finish(true);
	EXPECT_TRUE(identity.hasCanonicalDamageCalculator());
	EXPECT_EQ(identity.get(), spells::effects::ForecastImplementation::UNKNOWN);
}

TEST(LuaDamageCalculatorIdentity, AnyChangedLayerFailsClosedIncludingWhitespace)
{
	scripting::LuaModule host;
	for(size_t index = 0; index < 9; ++index)
	{
		auto script = calculator(host, canonicalPatches());
		script->layers[index].sourceText += "\n";
		EXPECT_FALSE(attests(*script)) << index;
	}
}

TEST(LuaDamageCalculatorIdentity, MissingExtraReorderedOrRelabeledLoadedLayersReject)
{
	scripting::LuaModule host;
	auto missing = calculator(host, canonicalPatches());
	missing->layers.erase(missing->layers.begin() + 3);
	EXPECT_FALSE(attests(*missing));
	auto extra = calculator(host, canonicalPatches());
	extra->layers.push_back(extra->layers.back());
	EXPECT_FALSE(attests(*extra));
	auto reordered = calculator(host, canonicalPatches());
	std::swap(reordered->layers[1], reordered->layers[2]);
	EXPECT_FALSE(attests(*reordered));
	auto relabeled = calculator(host, canonicalPatches());
	relabeled->layers[1].identifier = "mod:SCRIPTS/DAMAGE/SIEGEWEAPON";
	EXPECT_FALSE(attests(*relabeled));
}

TEST(LuaDamageCalculatorIdentity, ExactPatchDeclarationCountOrderAndScopeAreRequired)
{
	scripting::LuaModule host;
	const auto canonical = calculator(host, canonicalPatches());
	for(int change = 0; change < 5; ++change)
	{
		auto patches = canonicalPatches();
		if(change == 0)
			patches.pop_back();
		if(change == 1)
			patches.push_back(patches.back());
		if(change == 2)
			std::swap(patches[0], patches[1]);
		if(change == 3)
			patches[0].first = "root";
		if(change == 4)
			patches[0].second = "damage/magicElemental";
		auto script = calculator(host, patches);
		// Keep actual bytes canonical to isolate the declarations check.
		script->layers = canonical->layers;
		EXPECT_FALSE(attests(*script)) << change;
	}
}

TEST(LuaDamageCalculatorIdentity, MissingDeclaredPatchCannotBeHiddenBySkippedLoad)
{
	scripting::LuaModule host;
	auto patches = canonicalPatches();
	patches.emplace_back("core", "forecastIdentityDeliberatelyMissingPatch");
	auto script = calculator(host, patches);
	ASSERT_EQ(script->layers.size(), 9);
	ASSERT_EQ(script->getPatchDeclarations().size(), 9);
	EXPECT_FALSE(attests(*script));
}

TEST(LuaDamageCalculatorIdentity, AnyImportAttemptOrExecutionFailureInvalidates)
{
	scripting::LuaModule host;
	const auto script = calculator(host, canonicalPatches());
	for(const auto & name : {"spells/unitEffect", "battleLog", "unknown", ""})
	{
		auto identity = Identity::fromScript(*script);
		identity.moduleRequested(name);
		identity.finish(true);
		EXPECT_FALSE(identity.hasCanonicalDamageCalculator());
	}
	auto failed = Identity::fromScript(*script);
	failed.executionFailed();
	failed.finish(true);
	EXPECT_FALSE(failed.hasCanonicalDamageCalculator());
	auto late = Identity::fromScript(*script);
	late.finish(true);
	ASSERT_TRUE(late.hasCanonicalDamageCalculator());
	late.moduleRequested("unknown");
	EXPECT_FALSE(late.hasCanonicalDamageCalculator());
}

TEST(LuaDamageCalculatorIdentity, ActualContextAttestsOnlyAfterAllLayersExecute)
{
	scripting::LuaModule host;
	EmptyEnvironment environment;
	const auto script = calculator(host, canonicalPatches());
	scripting::LuaContext context(script.get(), &environment);
	EXPECT_FALSE(context.hasCanonicalDamageCalculator());
	context.initialize();
	ASSERT_TRUE(context.hasCanonicalDamageCalculator());
	const JsonNode parameters;
	EXPECT_FALSE(context.callMethod<std::vector<std::string>>("bonusTypes", parameters).empty());
	EXPECT_TRUE(context.hasCanonicalDamageCalculator());
	context.callFunction<void>("forecastIdentityDeliberatelyMissingMethod");
	EXPECT_FALSE(context.hasCanonicalDamageCalculator());
}
