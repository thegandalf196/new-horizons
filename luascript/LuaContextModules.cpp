/*
 * LuaContextModules.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "LuaContext.h"

#include "../lib/filesystem/Filesystem.h"
#include <vstd/StringUtils.h>

namespace scripting
{

int LuaContext::require(lua_State * L)
{
	auto * self = static_cast<LuaContext *>(lua_touserdata(L, lua_upvalueindex(1)));
	if(!self)
	{
		lua_pushstring(L, "internal error");
		return lua_error(L);
	}

	int result = self->loadModule();
	if(result < 0)
		return lua_error(L); // loadModule has already destroyed its locals.
	return result;
}

int LuaContext::loadModule()
{
	int argc = lua_gettop(L);
	// Observe every attempt, including invalid arguments, missing resources and
	// calls after initialization. Invalid identity never changes require behavior.
	implementationIdentity.moduleRequested(argc == 1 && lua_isstring(L, 1) ? toStringRaw(1) : std::string{});
	if(argc != 1)
	{
		lua_pushstring(L, "require: module name expected");
		return -1;
	}
	if(!lua_isstring(L, 1))
	{
		lua_pushstring(L, "require: module name must be a string");
		return -1;
	}

	std::string resourceName = toStringRaw(1);
	if(resourceName.empty())
	{
		lua_pushstring(L, "require: module name is empty");
		return -1;
	}

	auto temp = vstd::split(resourceName, ":");
	std::string scope;
	std::string modulePath;
	if(temp.size() <= 1)
		modulePath = temp.at(0);
	else
	{
		scope = temp.at(0);
		modulePath = temp.at(1);
	}

	auto * loader = scope.empty() ? CResourceHandler::get() : CResourceHandler::get(scope);
	ScriptPath id = ScriptPath::builtinTODO(modulePath).addPrefix("SCRIPTS/");
	if(!loader->existsResource(id))
	{
		lua_pushfstring(L, "require: module not found: %s", modulePath.c_str());
		return -1;
	}

	auto rawData = loader->load(id)->readAll();
	auto sourceText = std::string(reinterpret_cast<char *>(rawData.first.get()), rawData.second);
	implementationIdentity.moduleLoaded(resourceName, sourceText);
	int ret = luaL_loadbuffer(L, sourceText.c_str(), sourceText.size(), modulePath.c_str());
	if(ret)
	{
		implementationIdentity.executionFailed();
		return -1; // error string already on stack from luaL_loadbuffer
	}

	ret = lua_pcall(L, 0, 1, 0);
	if(ret)
	{
		implementationIdentity.executionFailed();
		return -1; // error string already on stack from lua_pcall
	}
	if(!lua_istable(L, -1))
	{
		implementationIdentity.executionFailed();
		lua_pushfstring(L, "require: module '%s' did not return a table", modulePath.c_str());
		return -1;
	}
	return 1;
}
}
