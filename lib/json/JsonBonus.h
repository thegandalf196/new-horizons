/*
 * JsonBonus.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "JsonNode.h"
#include "../GameConstants.h"
#include "../texts/TextIdentifier.h"

struct Bonus;
class ILimiter;
class CSelector;
class BonusParameters;
namespace JsonUtils
{
	std::shared_ptr<Bonus> parseBonus(const JsonVector & ability_vec);
	DLL_LINKAGE std::shared_ptr<Bonus> parseBonus(const JsonNode & ability, const TextIdentifier & descriptionID = "");
	DLL_LINKAGE bool parseBonus(const JsonNode & ability, Bonus * placement, const TextIdentifier & descriptionID = "");
	std::shared_ptr<const ILimiter> parseLimiter(const JsonNode & limiter);
	/// Selector from json fields. Identifiers it names may resolve after it is built; until they do it matches nothing.
	DLL_LINKAGE CSelector parseSelector(const JsonNode &ability);
}
