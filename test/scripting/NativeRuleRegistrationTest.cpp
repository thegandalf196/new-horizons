/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "../../Global.h"
#include <gtest/gtest.h>
#include "../../lib/combatScripts/ICombatEventScript.h"
#include "../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../lib/scripting/NativeScriptFactory.h"
#include "../../lib/spells/effects/Effect.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>

namespace
{
struct CatalogueFile
{
	const char * relativePath;
	const char * scope;
	size_t definitions;
};

constexpr CatalogueFile catalogueFiles[] = {
	{"config/scriptsSpells.json", "core", 40},
	{"config/scriptsCombat.json", "core", 17},
	{"Mods/new-horizons/Content/config/scripts/cabirRepair.json", "new-horizons", 1},
	{"Mods/new-horizons/Content/config/scripts/genieLuck.json", "new-horizons", 1}};

ScriptKind declaredKind(const std::string & value)
{
	if(value == "spellEffect") return ScriptKind::SPELL_EFFECT;
	if(value == "combatEvent") return ScriptKind::COMBAT_EVENT;
	if(value == "damageCalculator") return ScriptKind::DAMAGE_CALCULATOR;
	throw std::runtime_error("Unexpected implements value in shipped registry: " + value);
}

// This reproduces ScriptHandler's description construction, without its resource
// loader, text registration, identifier registration or global game library.
// Opening the four source files with standard streams makes this executable on
// a source checkout without original Heroes III assets or a Lua runtime.
std::vector<ScriptTypeDescription> loadShippedDescriptions()
{
	const char * root = std::getenv("NH_RULE_SOURCE_ROOT");
	if(!root || !*root)
		throw std::runtime_error("Set NH_RULE_SOURCE_ROOT to the New Horizons source checkout");
	std::vector<ScriptTypeDescription> result;
	for(const auto & file : catalogueFiles)
	{
		const auto path = std::filesystem::path(root) / file.relativePath;
		std::ifstream input(path, std::ios::binary);
		if(!input) throw std::runtime_error("Unable to read shipped rule registry: " + path.string());
		const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
		JsonParsingSettings settings;
		settings.mode = JsonParsingSettings::JsonFormatMode::JSONC;
		settings.strict = true;
		JsonNode document(bytes.data(), bytes.size(), settings, path.string());
		document.setModScope(file.scope);
		if(!document.isStruct() || document.Struct().size() != file.definitions)
			throw std::runtime_error("Shipped registry coverage changed: " + path.string());
		for(const auto & [identifier, data] : document.Struct())
		{
			if(!data.isStruct() || !data["script"].isString() || !data["implements"].isString()
				|| !data["patches"].isVector() || !data["schema"].isStruct())
				throw std::runtime_error("Incomplete shipped rule definition: " + identifier);
			ScriptTypeDescription description;
			description.identifier = identifier;
			description.modScope = file.scope;
			description.scriptId = description.modScope + ':' + identifier;
			description.sourcePath = data["script"].String();
			description.kind = declaredKind(data["implements"].String());
			description.parametersSchema = data["schema"];
			description.priority = data["priority"].isNull() ? 0 : static_cast<int>(data["priority"].Integer());
			for(const auto & patch : data["patches"].Vector())
				description.patches.emplace_back(patch.getModScope(), patch.String());
			if(!data["stringRegistrations"].isNull())
				for(const auto & registration : data["stringRegistrations"].Vector())
					description.stringRegistrations.push_back(registration.String());
			if(!data["description"].isNull())
				description.descriptionTextID = description.modScope + ".script." + identifier + ".description";
			result.push_back(std::move(description));
		}
	}
	return result;
}

using Coverage = std::map<std::string, std::pair<ScriptKind, std::string>>;

Coverage expectedCoverage()
{
	Coverage expected;
	const std::pair<const char *, const char *> coreSpells[] = {
		{"earthquake", "earthquake"}, {"catapult", "catapult"}, {"clone", "clone"},
		{"damage", "damage"}, {"havocStructures", "havocStructures"}, {"demonSummon", "demonSummon"},
		{"dispel", "dispel"}, {"heal", "heal"}, {"lifeDrainEffect", "lifeDrain"},
		{"hexOfPainEffect", "hexOfPain"}, {"plagueEffect", "plague"}, {"soulChainEffect", "soulChain"},
		{"shadowGiftEffect", "shadowGift"}, {"vampirismEffect", "vampirism"}, {"reanimateEffect", "reanimate"},
		{"doomEffect", "doom"}, {"frailtyEffect", "frailty"}, {"sacrifice", "sacrifice"},
		{"moat", "moat"}, {"obstacle", "obstacle"}, {"removeObstacle", "removeObstacle"},
		{"summon", "summon"}, {"timedSummon", "timedSummon"}, {"summonTrolls", "summonTrolls"},
		{"elementalConvergence", "elementalConvergence"}, {"pandemonium", "pandemonium"},
		{"naturesWrath", "naturesWrath"}, {"blink", "blink"}, {"hydrasVitality", "hydrasVitality"},
		{"verdantPrison", "verdantPrison"}, {"transfigureMatter", "transfigureMatter"},
		{"teleport", "teleport"}, {"attachCombatScript", "attachCombatScript"},
		{"theChosenEffect", "theChosen"}, {"timed", "timed"}, {"phantomArmy", "phantomArmy"},
		{"timeStop", "timeStop"}, {"purify", "purify"}, {"spellLock", "spellLock"},
		{"focusMagicEnchantment", "focusMagic"}};
	for(const auto & [id, path] : coreSpells)
		expected.emplace(std::string("core:") + id, std::make_pair(ScriptKind::SPELL_EFFECT, std::string("spells/") + path));
	for(const auto * id : {"transmutation", "summonGuardians", "enchanted", "lifeDrain", "arrowTowerDamage",
		"fireShield", "hexOfPain", "deathStare", "destruction", "soulSteal", "focusMagic", "arcaneBreach",
		"plagueStatus", "soulChainStatus", "shadowGift", "vampirism"})
		expected.emplace(std::string("core:") + id, std::make_pair(ScriptKind::COMBAT_EVENT, std::string("combat/") + id));
	expected.emplace("core:damageCalculator", std::make_pair(ScriptKind::DAMAGE_CALCULATOR, "damage/damageCalculator"));
	for(const auto * id : {"cabirRepair", "genieLuck"})
		expected.emplace(std::string("new-horizons:") + id, std::make_pair(ScriptKind::SPELL_EFFECT, std::string("spells/") + id));
	return expected;
}

void expectCreation(const NativeScriptFactory & factory, const ScriptTypeDescription & description)
{
	SCOPED_TRACE(description.scriptId);
	switch(description.kind)
	{
		case ScriptKind::SPELL_EFFECT:
			EXPECT_NE(factory.createSpellEffect(description.scriptId), nullptr);
			break;
		case ScriptKind::COMBAT_EVENT:
			EXPECT_NE(factory.createCombatEventScript(description.scriptId), nullptr);
			break;
		case ScriptKind::DAMAGE_CALCULATOR:
			EXPECT_NE(factory.createDamageCalculatorScript(description.scriptId), nullptr);
			break;
		default:
			FAIL() << "Unexpected rule kind in registration coverage";
	}
}
}

TEST(NativeRuleRegistrationTest, EveryShippedDefinitionInitializesAndCreatesItsDeclaredInterface)
{
	std::vector<ScriptTypeDescription> descriptions;
	ASSERT_NO_THROW(descriptions = loadShippedDescriptions());
	ASSERT_EQ(descriptions.size(), 59u);
	NativeScriptFactory factory;
	Coverage actual;
	std::array<size_t, 3> counts{};
	std::set<std::string> patched;
	for(const auto & description : descriptions)
	{
		SCOPED_TRACE(description.scriptId);
		EXPECT_TRUE(actual.emplace(description.scriptId, std::make_pair(description.kind, description.sourcePath)).second);
		ASSERT_LT(static_cast<size_t>(description.kind), counts.size());
		++counts[static_cast<size_t>(description.kind)];
		EXPECT_EQ(description.scriptId, description.modScope + ':' + description.identifier);
		EXPECT_EQ(description.parametersSchema.getModScope(), description.modScope);
		if(!description.patches.empty()) patched.insert(description.scriptId);
		ASSERT_NO_THROW(factory.initialize(description));
		ASSERT_NO_FATAL_FAILURE(expectCreation(factory, description));
	}
	EXPECT_EQ(actual, expectedCoverage());
	EXPECT_EQ(counts[static_cast<size_t>(ScriptKind::SPELL_EFFECT)], 42u);
	EXPECT_EQ(counts[static_cast<size_t>(ScriptKind::COMBAT_EVENT)], 16u);
	EXPECT_EQ(counts[static_cast<size_t>(ScriptKind::DAMAGE_CALCULATOR)], 1u);
	const std::set<std::string> expectedPatched{"core:timed", "core:deathStare", "core:damageCalculator"};
	EXPECT_EQ(patched, expectedPatched);
	// Creation remains valid after the entire catalogue has been registered;
	// in particular, spell and combat identities with related names coexist.
	for(const auto & description : descriptions)
		ASSERT_NO_FATAL_FAILURE(expectCreation(factory, description));
}

TEST(NativeRuleRegistrationTest, UnknownPathsKindsAndAddedPatchesRejectWithoutReplacingValidDefinitions)
{
	std::vector<ScriptTypeDescription> descriptions;
	ASSERT_NO_THROW(descriptions = loadShippedDescriptions());
	NativeScriptFactory factory;
	for(const auto & original : descriptions)
	{
		SCOPED_TRACE(original.scriptId);
		ASSERT_NO_THROW(factory.initialize(original));
		auto changed = original;
		changed.sourcePath += "/unknown";
		EXPECT_THROW(factory.initialize(changed), std::runtime_error);
		ASSERT_NO_FATAL_FAILURE(expectCreation(factory, original));
		changed = original;
		changed.kind = ScriptKind::INVALID;
		EXPECT_THROW(factory.initialize(changed), std::runtime_error);
		ASSERT_NO_FATAL_FAILURE(expectCreation(factory, original));
		changed = original;
		changed.patches.emplace_back("core", "unsupported/patch");
		EXPECT_THROW(factory.initialize(changed), std::runtime_error);
		ASSERT_NO_FATAL_FAILURE(expectCreation(factory, original));
	}
	EXPECT_THROW(factory.createSpellEffect("core:unknown"), std::out_of_range);
	EXPECT_THROW(factory.createCombatEventScript("core:unknown"), std::out_of_range);
	EXPECT_THROW(factory.createDamageCalculatorScript("core:unknown"), std::out_of_range);
}

TEST(NativeRuleRegistrationTest, DeclaredPatchChainsRequireEveryLayerItsScopeAndItsOrder)
{
	std::vector<ScriptTypeDescription> descriptions;
	ASSERT_NO_THROW(descriptions = loadShippedDescriptions());
	NativeScriptFactory factory;
	size_t checkedChains = 0, checkedLayers = 0;
	for(const auto & original : descriptions)
	{
		if(original.patches.empty()) continue;
		SCOPED_TRACE(original.scriptId);
		ASSERT_NO_THROW(factory.initialize(original));
		++checkedChains;
		for(size_t index = 0; index < original.patches.size(); ++index)
		{
			SCOPED_TRACE(index);
			++checkedLayers;
			auto changed = original;
			changed.patches[index].first = "new-horizons";
			EXPECT_THROW(factory.initialize(changed), std::runtime_error);
			changed = original;
			changed.patches[index].second += "/unknown";
			EXPECT_THROW(factory.initialize(changed), std::runtime_error);
			changed = original;
			changed.patches.erase(changed.patches.begin() + index);
			EXPECT_THROW(factory.initialize(changed), std::runtime_error);
		}
		auto changed = original;
		changed.patches.push_back(changed.patches.back());
		EXPECT_THROW(factory.initialize(changed), std::runtime_error);
		changed = original;
		changed.patches.clear();
		EXPECT_THROW(factory.initialize(changed), std::runtime_error);
		if(original.patches.size() > 1)
		{
			changed = original;
			std::swap(changed.patches.front(), changed.patches.back());
			EXPECT_THROW(factory.initialize(changed), std::runtime_error);
		}
		ASSERT_NO_FATAL_FAILURE(expectCreation(factory, original));
	}
	EXPECT_EQ(checkedChains, 3u);
	EXPECT_EQ(checkedLayers, 12u);
}
