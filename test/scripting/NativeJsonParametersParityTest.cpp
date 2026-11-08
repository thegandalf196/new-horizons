/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../luascript/StdInc.h"
#include "../../luascript/LuaStack.h"
#include "../../luascript/api/library/BonusDescriptor.h"
#include "../../lib/combatScripts/NativeJsonParameters.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <random>
#include <iostream>
#include <set>

namespace
{
void expectSameNode(const JsonNode & actual, const JsonNode & expected)
{
	ASSERT_EQ(actual.getType(), expected.getType());
	EXPECT_EQ(actual.getModScope(), expected.getModScope());
	if(expected.isStruct())
	{
		ASSERT_EQ(actual.Struct().size(), expected.Struct().size());
		for(const auto & [key, value] : expected.Struct())
		{
			SCOPED_TRACE(key);
			ASSERT_TRUE(actual.Struct().contains(key));
			expectSameNode(actual[key], value);
		}
	}
	else if(expected.isVector())
	{
		ASSERT_EQ(actual.Vector().size(), expected.Vector().size());
		for(size_t index = 0; index < expected.Vector().size(); ++index) expectSameNode(actual.Vector()[index], expected.Vector()[index]);
	}
	else EXPECT_EQ(actual, expected);
}

JsonNode generated(std::mt19937 & random, int depth)
{
	JsonNode result;
	const auto choice = random() % (depth > 0 ? 7 : 5);
	if(choice == 1) result.Bool() = (random() & 1) != 0;
	if(choice == 2) result.Integer() = static_cast<int64_t>(random()) - 2000000000;
	if(choice == 3) result.Float() = static_cast<double>(random()) / 37.0;
	if(choice == 4) result.String() = "value" + std::to_string(random() % 100);
	if(choice == 5)
	{
		result.Vector();
		const auto count = random() % 6;
		for(uint32_t index = 0; index < count; ++index) result.Vector().push_back(generated(random, depth - 1));
	}
	if(choice == 6)
	{
		result.Struct();
		const auto count = random() % 6;
		for(uint32_t index = 0; index < count; ++index) result["field" + std::to_string(index)] = generated(random, depth - 1);
	}
	result.setModScope("source", false);
	return result;
}

TEST(NativeJsonParametersParity, CapturedTreesMatchActualInterpreterConversionIncludingTypesAndScopes)
{
	std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), &lua_close);
	ASSERT_NE(state, nullptr);
	scripting::LuaStack stack(state.get());
	std::mt19937 random(0x5a73f2);
	for(int index = 0; index < 2000; ++index)
	{
		SCOPED_TRACE(index);
		const auto source = generated(random, 4);
		stack.push(source);
		JsonNode expected;
		stack.get(1, expected);
		expectSameNode(nativeRules::capturedParameters(source), expected);
		stack.clear();
	}
}

TEST(NativeJsonParametersParity, BonusDescriptorPreservesEmptyFieldOmissionAndIntegerValAndTurns)
{
	std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), &lua_close);
	ASSERT_NE(state, nullptr);
	scripting::LuaStack stack(state.get());
	for(const auto * type : {"MORALE", "BIND_EFFECT", "RANDOM_SPELLCASTER"})
	{
		SCOPED_TRACE(type);
		JsonNode source;
		source["type"].String() = type;
		source["val"].Float() = 3.75;
		source["turns"].Float() = 7.5;
		source["duration"].String() = "N_TURNS";
		source["addInfo"].Struct();
		source["statusTags"].Vector();
		source["limiters"].Struct();
		stack.push(source);
		scripting::api::BonusDescriptor descriptor;
		stack.get(1, descriptor);
		auto expected = descriptor.toBonus();
		auto actual = nativeRules::bonusFromDescriptor(source);
		CMemorySerializer expectedWire;
		CMemorySerializer actualWire;
		expectedWire.oser & expected;
		actualWire.oser & actual;
		EXPECT_EQ(actualWire.extractBuffer(), expectedWire.extractBuffer());
		stack.clear();
	}
}

TEST(NativeJsonParametersParity, ReportsFixedTimedSiblingPairsOrderAcrossFreshInterpreterStates)
{
	struct Family
	{
		const char * name;
		std::vector<std::string> source, convertedAdditions, recipientAdditions;
	};
	const std::vector<Family> families{
		{"stoneGaze", {"notActive", "noRetaliation", "generalDamageReduction"}, {}, {}},
		{"paralyze", {"notActive", "noRetaliation", "generalAttackReduction"}, {}, {}},
		{"disease", {"physicalAffliction", "attack", "defence"}, {}, {}},
		{"poison", {"poison", "stackHealth"}, {}, {}},
		{"prayer", {"attack", "defence", "stacksSpeed"}, {}, {}},
		{"blindExpert", {"notActive", "generalAttackReduction", "noRetaliation"}, {}, {}},
		{"crusade", {"attack", "defense", "initiative", "magicalDamageReduction", "moraleFloor"}, {}, {}},
		{"shieldOfChaos", {"physicalDamageReduction", "magicalDamageReduction", "morale", "luck"}, {}, {}},
		{"slow", {"stacksSpeed"}, {"slowSpeed"}, {}},
		{"misfortune", {"luck"}, {"favorableCreatureChanceMultiplier"}, {}},
		{"forgetfulness", {"forgetful"}, {"creatureAbilitySuppression"}, {}},
		{"sanctuaryKeeper", {"sanctified"}, {}, {"sanctuaryKeeperMorale"}}
	};
	// Observe the real interpreter's pairs operation at the three key-map
	// boundaries used by timed.lua. This is order evidence, not effect execution
	// or a claim that a fresh interpreter must use a different hash seed.
	constexpr auto observe = R"lua(
		return function(source, conversion, recipient)
			local converted = {}
			for name, value in pairs(source) do converted[name] = value end
			for _, name in ipairs(conversion) do converted[name] = 1 end
			local buffer = {}
			for name, value in pairs(converted) do buffer[name] = value end
			for _, name in ipairs(recipient) do buffer[name] = 1 end
			local order = {}
			for name in pairs(buffer) do order[#order + 1] = name end
			return order
		end
	)lua";
	std::vector<std::set<std::string>> observed(families.size());
	for(int instance = 0; instance < 32; ++instance)
	{
		std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), &lua_close);
		ASSERT_NE(state, nullptr);
		luaL_openlibs(state.get());
		ASSERT_EQ(luaL_loadstring(state.get(), observe), 0);
		ASSERT_EQ(lua_pcall(state.get(), 0, 1, 0), 0);
		const int function = luaL_ref(state.get(), LUA_REGISTRYINDEX);
		scripting::LuaStack stack(state.get());
		for(size_t index = 0; index < families.size(); ++index)
		{
			const auto & family = families[index];
			SCOPED_TRACE(family.name);
			JsonNode source, conversion, recipient;
			for(const auto & key : family.source) source[key].Integer() = 1;
			conversion.Vector(); recipient.Vector();
			for(const auto & key : family.convertedAdditions) conversion.Vector().emplace_back(key);
			for(const auto & key : family.recipientAdditions) recipient.Vector().emplace_back(key);
			lua_rawgeti(state.get(), LUA_REGISTRYINDEX, function);
			stack.push(source); stack.push(conversion); stack.push(recipient);
			ASSERT_EQ(lua_pcall(state.get(), 3, 1, 0), 0) << lua_tostring(state.get(), -1);
			JsonNode result;
			stack.get(1, result);
			std::set<std::string> expected(family.source.begin(), family.source.end()), actual;
			expected.insert(family.convertedAdditions.begin(), family.convertedAdditions.end());
			expected.insert(family.recipientAdditions.begin(), family.recipientAdditions.end());
			std::string order;
			for(const auto & key : result.Vector())
			{
				actual.insert(key.String());
				if(!order.empty()) order += ',';
				order += key.String();
			}
			EXPECT_EQ(actual, expected);
			EXPECT_EQ(result.Vector().size(), expected.size());
			observed[index].insert(order);
			RecordProperty(std::string(family.name) + "_state_" + std::to_string(instance), order);
			std::cout << "TIMED_PAIRS state=" << instance << " family=" << family.name << " order=" << order << '\n';
			stack.clear();
		}
	}
	for(size_t index = 0; index < families.size(); ++index)
		std::cout << "TIMED_PAIRS_SUMMARY family=" << families[index].name << " distinct_orders=" << observed[index].size()
			<< " states=32 (observation only; no variability requirement)\n";
}
}
