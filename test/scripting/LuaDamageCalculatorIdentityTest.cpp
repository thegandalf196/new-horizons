/*
 * LuaDamageCalculatorIdentityTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HistoricalDamageCalculatorScript.h"
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
	for(const auto & path : {"damage/physicalProtection", "damage/siegeWeapon", "damage/magicElemental", "damage/psychicElemental",
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


void replaceRequired(std::string & source, std::string_view current, std::string_view previous)
{
	const auto position = source.find(current);
	ASSERT_NE(position, std::string::npos);
	ASSERT_EQ(source.find(current, position + 1), std::string::npos);
	source.replace(position, current.size(), previous);
}

// Restore the exact pre-Inspiration pair, not an arbitrary compatible Lua program.
void restorePriorPointBlankPair(scripting::LuaScriptInstance & script)
{
	auto & root = script.layers.front().sourceText;
	ASSERT_NO_FATAL_FAILURE(replaceRequired(root,
		"\tif targetDefense then\n"
		"\t\tignored = ignored + math.floor((info.arcaneInspirationDefenseIgnorePercent or 0) * math.max(0, defense) / 100)\n"
		"\tend\n", ""));
	ASSERT_NO_FATAL_FAILURE(replaceRequired(root,
		"\tlocal inspirationMultiplier = (100 + (info.arcaneInspirationDamageBonusPercent or 0)) / 100\n"
		"\t\t* (100 - (info.spellshockOutputPenaltyPercent or 0)) / 100\n"
		"\tlocal damageFactor = raising * lowering * physicalDamageReductionMultiplier * inspirationMultiplier\n",
		"\tlocal damageFactor = raising * lowering * physicalDamageReductionMultiplier\n"));
	ASSERT_NO_FATAL_FAILURE(replaceRequired(root,
		"\t\t* sylvanMultiplier * ordinaryCreatureSpecialtyMultiplier * finalLuckMultiplier * inspirationMultiplier\n",
		"\t\t* sylvanMultiplier * ordinaryCreatureSpecialtyMultiplier * finalLuckMultiplier\n"));
	ASSERT_NO_FATAL_FAILURE(replaceRequired(script.layers[1].sourceText,
		"\ttable.insert(reductions, { info.arcaneBulwarkReductionPercent or 0, 100 })\n", ""));
}

class EmptyEnvironment final : public Environment
{
public:
	const Services * services() const override { return nullptr; }
	const GameCb * game() const override { return nullptr; }
	const BattleCb * battle(const BattleID &) const override { return nullptr; }
};
}

TEST(LuaDamageCalculatorIdentity, CompleteOrderedTenLayerChainRequiresSuccessfulInitialization)
{
	scripting::LuaModule host;
	const auto script = calculator(host, canonicalPatches());
	ASSERT_EQ(script->layers.size(), 10);
	ASSERT_EQ(script->getPatchDeclarations().size(), 9);
	auto identity = Identity::fromScript(*script);
	EXPECT_FALSE(identity.hasCanonicalDamageCalculator());
	identity.finish(true);
	EXPECT_TRUE(identity.hasCanonicalDamageCalculator());
	EXPECT_EQ(identity.get(), spells::effects::ForecastImplementation::UNKNOWN);
}

TEST(LuaDamageCalculatorIdentity, AnyChangedLayerFailsClosedIncludingWhitespace)
{
	scripting::LuaModule host;
	for(size_t index = 0; index < 10; ++index)
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
	missing->layers.erase(missing->layers.begin() + 1);
	EXPECT_FALSE(attests(*missing));
	auto extra = calculator(host, canonicalPatches());
	extra->layers.push_back(extra->layers.back());
	EXPECT_FALSE(attests(*extra));
	auto reordered = calculator(host, canonicalPatches());
	std::swap(reordered->layers[1], reordered->layers[2]);
	EXPECT_FALSE(attests(*reordered));
	auto relabeled = calculator(host, canonicalPatches());
	relabeled->layers[1].identifier = "mod:SCRIPTS/DAMAGE/PHYSICALPROTECTION";
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
	ASSERT_EQ(script->layers.size(), 10);
	ASSERT_EQ(script->getPatchDeclarations().size(), 10);
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


TEST(LuaDamageCalculatorIdentity, PreviousPhysicalProtectionRootRetainsItsExactChain)
{
	scripting::LuaModule host;
	auto script = calculator(host, canonicalPatches());
	ASSERT_NO_FATAL_FAILURE(restorePriorPointBlankPair(*script));
	auto & source = script->layers.front().sourceText;
	const std::string current = "\t\t\tif info.archeryIgnoreAdjacentRangedPenalty then\n"
		"\t\t\t\tif info.pointBlankPreservesAdjacentExemption then return 0 end\n"
		"\t\t\t\treturn info.targetedRangedCommand and -0.25 or 0\n\t\t\tend";
	const auto position = source.find(current);
	ASSERT_NE(position, std::string::npos);
	source.replace(position, current.size(),
		"\t\t\tif info.archeryIgnoreAdjacentRangedPenalty then return info.targetedRangedCommand and -0.25 or 0 end");
	EXPECT_TRUE(attests(*script));
	script->layers[1].sourceText += "\n";
	EXPECT_FALSE(attests(*script));
}


TEST(LuaDamageCalculatorIdentity, HistoricalNineLayerContextLoadsExactRootAndRejectsByteDrift)
{
	scripting::LuaModule host;
	EmptyEnvironment environment;
	auto patches = canonicalPatches();
	ASSERT_EQ(patches.front().second, "damage/physicalProtection");
	patches.erase(patches.begin());
	ASSERT_EQ(patches.size(), 8);
	for(const bool changed : {false, true})
	{
		auto script = calculator(host, patches);
		ASSERT_EQ(script->layers.size(), 9);
		ASSERT_EQ(script->getPatchDeclarations().size(), 8);
		// Same fixture pattern as historical Timed: preserve loaded canonical
		// layer identities, supplying exact historical bytes before Lua executes.
		script->layers.front().sourceText = std::string(historicalDamageCalculatorIdentityTest::SOURCE)
			+ (changed ? "\n-- unreviewed historical root change\n" : "");
		scripting::LuaContext context(script.get(), &environment);
		EXPECT_FALSE(context.hasCanonicalDamageCalculator());
		context.initialize();
		EXPECT_EQ(context.hasCanonicalDamageCalculator(), !changed);
		const JsonNode parameters;
		EXPECT_FALSE(context.callMethod<std::vector<std::string>>("bonusTypes", parameters).empty());
		EXPECT_EQ(context.hasCanonicalDamageCalculator(), !changed);
	}
}

TEST(LuaDamageCalculatorIdentity, InspirationAndPriorPointBlankPairsAttestButBothMixedPairsFailClosed)
{
	scripting::LuaModule host;
	const auto current = calculator(host, canonicalPatches());
	ASSERT_TRUE(attests(*current));
	auto previous = calculator(host, canonicalPatches());
	ASSERT_NO_FATAL_FAILURE(restorePriorPointBlankPair(*previous));
	ASSERT_TRUE(attests(*previous));
	auto oldRootWithCurrentProtection = calculator(host, canonicalPatches());
	oldRootWithCurrentProtection->layers[0].sourceText = previous->layers[0].sourceText;
	EXPECT_FALSE(attests(*oldRootWithCurrentProtection));
	auto currentRootWithOldProtection = calculator(host, canonicalPatches());
	currentRootWithOldProtection->layers[1].sourceText = previous->layers[1].sourceText;
	EXPECT_FALSE(attests(*currentRootWithOldProtection));
}
