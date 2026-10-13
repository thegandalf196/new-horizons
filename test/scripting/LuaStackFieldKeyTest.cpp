/*
 * LuaStackFieldKeyTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../../luascript/StdInc.h"
#include "../../luascript/LuaStack.h"

#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Snapshot = std::map<std::string, std::string>;

void capture(lua_State * state, int index, const std::string & prefix, Snapshot & result)
{
	if(index < 0)
		index = lua_gettop(state) + index + 1;
	lua_pushnil(state);
	while(lua_next(state, index))
	{
		std::string key;
		if(lua_type(state, -2) == LUA_TSTRING)
		{
			size_t length = 0;
			const char * text = lua_tolstring(state, -2, &length);
			key = "s:" + std::string(text, length);
		}
		else
			key = "i:" + std::to_string(lua_tointeger(state, -2)); // Do not stringify lua_next's numeric key.
		const auto path = prefix + "/" + key;
		if(lua_istable(state, -1))
		{
			result[path] = "table";
			capture(state, -1, path, result);
		}
		else if(lua_isboolean(state, -1))
			result[path] = lua_toboolean(state, -1) ? "true" : "false";
		else
		{
			size_t length = 0;
			const char * text = lua_tolstring(state, -1, &length);
			result[path] = text ? std::string(text, length) : "nil";
		}
		lua_pop(state, 1);
	}
}

template<typename T>
void legacyPush(lua_State * state, T & value)
{
	lua_newtable(state);
	const int table = lua_gettop(state);
	scripting::LuaStack stack(state);
	const auto writer = [&]<typename Field>(const std::string & key, const Field & data, std::string_view)
	{
		stack.push(data);
		lua_setfield(state, table, key.c_str());
	};
	value.serializeScript(writer);
}

struct LiteralFields : scripting::TagSerializable
{
	template<typename Handler>
	void serializeScript(Handler handler)
	{
		handler("short", 11, "");
		handler("long_literal_field_name_far_beyond_any_small_string_optimization_capacity", 22, "");
		handler("embedded\0ignored_suffix", 33, "");
	}
};

struct DynamicFields : scripting::TagSerializable
{
	std::string key = "dynamic_field_name_far_beyond_small_string_capacity";
	template<typename Handler>
	void serializeScript(Handler handler)
	{
		handler(key, 44, "");
	}
};

struct MutateString : scripting::TagSerializable
{
	std::string * key = nullptr;
	template<typename Handler>
	void serializeScript(Handler handler)
	{
		*key = "string_after";
		handler("value", 45, "");
	}
};

struct StringMutationFields : scripting::TagSerializable
{
	std::string key = "string_before";
	template<typename Handler>
	void serializeScript(Handler handler)
	{
		MutateString nested;
		nested.key = &key;
		handler(key, nested, "");
	}
};

struct MutateArray : scripting::TagSerializable
{
	char * key = nullptr;
	template<typename Handler>
	void serializeScript(Handler handler)
	{
		std::strcpy(key, "array_after");
		handler("value", 55, "");
	}
};

struct MutableArrayFields : scripting::TagSerializable
{
	char key[32] = "array_before";
	template<typename Handler>
	void serializeScript(Handler handler)
	{
		MutateArray nested;
		nested.key = key;
		handler(key, nested, "");
	}
};

template<size_t Capacity>
struct ConstViewArrayFields : scripting::TagSerializable
{
	char key[Capacity] = "array_before";
	template<typename Handler>
	void serializeScript(Handler handler)
	{
		MutateArray nested;
		nested.key = key;
		handler(std::as_const(key), nested, ""); // Const view does not make the backing array immutable.
	}
};

struct MutableBufferPointerFields : scripting::TagSerializable
{
	char key[32] = "array_before";
	template<typename Handler>
	void serializeScript(Handler handler)
	{
		const char * pointer = key;
		MutateArray nested;
		nested.key = key;
		handler(pointer, nested, "");
	}
};

struct MutatePointer : scripting::TagSerializable
{
	const char ** key = nullptr;
	template<typename Handler>
	void serializeScript(Handler handler)
	{
		*key = "pointer_after";
		handler("value", 66, "");
	}
};

struct PointerFields : scripting::TagSerializable
{
	const char * key = "pointer_before";
	template<typename Handler>
	void serializeScript(Handler handler)
	{
		MutatePointer nested;
		nested.key = &key;
		handler(key, nested, "");
	}
};

struct StringKey
{
	operator std::string() const
	{
		return "implicit_string";
	}
};
struct DualKey
{
	operator const char *() const
	{
		return "wrong_pointer_conversion";
	}
	operator std::string() const
	{
		return "correct_string_conversion";
	}
};
struct LvalueKey
{
	operator std::string() &
	{
		return "lvalue_string";
	}
};
struct RvalueKey
{
	operator std::string() &&
	{
		return "rvalue_string";
	}
};
struct ConvertibleFields : scripting::TagSerializable
{
	template<typename Handler>
	void serializeScript(Handler handler)
	{
		StringKey implicit;
		DualKey dual;
		LvalueKey lvalue;
		handler(implicit, 77, "");
		handler(dual, 88, "");
		handler(lvalue, 99, "");
		handler(RvalueKey{}, 111, "");
	}
};

struct NestedFields : scripting::TagSerializable
{
	std::vector<int> values{1, 2, 3};
	std::map<std::string, int> mapping{{"first", 4}, {"second", 5}};
	LiteralFields nested;
	template<typename Handler>
	void serializeScript(Handler handler)
	{
		handler("vector", values, "");
		handler("map", mapping, "");
		handler("nested", nested, "");
		handler("boolean", true, "");
		handler("text", std::string("payload"), "");
	}
};
}

class LuaStackFieldKeyTest : public testing::Test
{
protected:
	lua_State * state = nullptr;
	void SetUp() override
	{
		state = luaL_newstate();
		ASSERT_NE(state, nullptr);
	}
	void TearDown() override
	{
		if(state)
			lua_close(state);
	}

	template<typename T>
	Snapshot compareLegacy()
	{
		T legacy;
		T current;
		lua_pushinteger(state, 901); // Preserve unrelated caller stack content.
		scripting::LuaStack stack(state);
		legacyPush(state, legacy);
		Snapshot expected;
		capture(state, -1, "", expected);
		EXPECT_EQ(lua_gettop(state), 2);
		lua_pop(state, 1);
		stack.push(current); // Actual production TagSerializable writer, not copied optimized code.
		Snapshot actual;
		capture(state, -1, "", actual);
		EXPECT_EQ(lua_gettop(state), 2);
		EXPECT_EQ(actual, expected);
		stack.restoreInitialTop();
		EXPECT_EQ(lua_gettop(state), 1);
		EXPECT_EQ(lua_tointeger(state, -1), 901);
		lua_pop(state, 1);
		EXPECT_EQ(lua_gettop(state), 0);
		return actual;
	}
};

TEST_F(LuaStackFieldKeyTest, ConstLiteralShortLongAndEmbeddedNulMatchLegacy)
{
	const auto values = compareLegacy<LiteralFields>();
	EXPECT_EQ(values.at("/s:short"), "11");
	EXPECT_EQ(values.at("/s:long_literal_field_name_far_beyond_any_small_string_optimization_capacity"), "22");
	EXPECT_EQ(values.at("/s:embedded"), "33"); // Existing lua_setfield C-string truncation is intentional parity.
	EXPECT_EQ(values.size(), 3u);
}

TEST_F(LuaStackFieldKeyTest, DynamicStringKeyMatchesLegacy)
{
	const auto values = compareLegacy<DynamicFields>();
	EXPECT_EQ(values.at("/s:dynamic_field_name_far_beyond_small_string_capacity"), "44");
}

TEST_F(LuaStackFieldKeyTest, ExistingStringReferenceRetainsLegacyNestedMutation)
{
	const auto values = compareLegacy<StringMutationFields>();
	EXPECT_EQ(values.at("/s:string_after/s:value"), "45"); // Legacy binds the existing std::string by reference.
	EXPECT_FALSE(values.contains("/s:string_before"));
}

TEST_F(LuaStackFieldKeyTest, MutableArrayKeySnapshotsBeforeNestedMutation)
{
	const auto values = compareLegacy<MutableArrayFields>();
	EXPECT_EQ(values.at("/s:array_before/s:value"), "55");
	EXPECT_FALSE(values.contains("/s:array_after"));
}

TEST_F(LuaStackFieldKeyTest, ConstViewOfMutableArraySnapshotsBeforeNestedMutation)
{
	const auto values = compareLegacy<ConstViewArrayFields<32>>();
	EXPECT_EQ(values.at("/s:array_before/s:value"), "55");
	EXPECT_FALSE(values.contains("/s:array_after"));
	const auto largeValues = compareLegacy<ConstViewArrayFields<300>>();
	EXPECT_EQ(largeValues.at("/s:array_before/s:value"), "55");
	EXPECT_FALSE(largeValues.contains("/s:array_after")); // Proposed bounded fast path must retain large-array fallback.
}

TEST_F(LuaStackFieldKeyTest, ConstCharPointerToMutableBufferSnapshotsBeforeNestedMutation)
{
	const auto values = compareLegacy<MutableBufferPointerFields>();
	EXPECT_EQ(values.at("/s:array_before/s:value"), "55");
	EXPECT_FALSE(values.contains("/s:array_after"));
}

TEST_F(LuaStackFieldKeyTest, PointerKeySnapshotsBeforeNestedMutation)
{
	const auto values = compareLegacy<PointerFields>();
	EXPECT_EQ(values.at("/s:pointer_before/s:value"), "66");
	EXPECT_FALSE(values.contains("/s:pointer_after"));
}

TEST_F(LuaStackFieldKeyTest, ImplicitDualAndRefQualifiedStringConversionsMatchLegacy)
{
	const auto values = compareLegacy<ConvertibleFields>();
	EXPECT_EQ(values.at("/s:implicit_string"), "77");
	EXPECT_EQ(values.at("/s:correct_string_conversion"), "88");
	EXPECT_EQ(values.at("/s:lvalue_string"), "99");
	EXPECT_EQ(values.at("/s:rvalue_string"), "111");
	EXPECT_FALSE(values.contains("/s:wrong_pointer_conversion"));
}

TEST_F(LuaStackFieldKeyTest, NestedMapVectorValuesAndStackBalanceMatchLegacy)
{
	const auto values = compareLegacy<NestedFields>();
	EXPECT_EQ(values.at("/s:vector/i:1"), "1");
	EXPECT_EQ(values.at("/s:vector/i:3"), "3");
	EXPECT_EQ(values.at("/s:map/s:second"), "5");
	EXPECT_EQ(values.at("/s:nested/s:embedded"), "33");
	EXPECT_EQ(values.at("/s:boolean"), "true");
	EXPECT_EQ(values.at("/s:text"), "payload");
}
