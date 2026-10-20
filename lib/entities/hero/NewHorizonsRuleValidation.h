/*
 * NewHorizonsRuleValidation.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../json/JsonNode.h"
#include "../../GameLibrary.h"
#include "../../modding/IdentifierStorage.h"
#include "../../modding/ModScope.h"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>

namespace newHorizonsHeroes::detail
{
inline void require(bool valid, const std::string & detail)
{
	if(!valid)
		throw std::runtime_error("Invalid New Horizons hero rules: " + detail);
}

inline bool integer(const JsonNode & value, int minimum, int maximum)
{
	return value.isNumber() && std::isfinite(value.Float()) && value.Float() >= minimum
		&& value.Float() <= maximum && std::floor(value.Float()) == value.Float();
}

inline int resolve(const std::string & type, const std::string & key)
{
	const auto separator = key.find(':');
	require(separator != std::string::npos && separator > 0 && separator + 1 < key.size(), "scoped " + type);
	const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), type, key, true);
	require(id.has_value() && *id >= 0, "unknown " + type + " " + key);
	return *id;
}

inline void fields(const JsonNode & node, std::initializer_list<std::string_view> allowed)
{
	require(node.isStruct(), "object required");
	for(const auto & [key, value] : node.Struct())
		require(std::find(allowed.begin(), allowed.end(), key) != allowed.end(), "unknown field " + key);
}

}
