/*
 * HeroCommand.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../json/JsonNode.h"

struct Bonus;
class CGHeroInstance;

/// Stable identifiers in the versioned New Horizons combat ruleset.
enum class HeroCommand : int8_t
{
	NONE = 0,
	CHARGE = 1,
	HOLD_THE_LINE = 2,
	ADVANCE = 3,
	AGGRESSIVE = 4,
	DEFENSIVE = 5
};

namespace heroCommands
{
constexpr int RULESET_VERSION = 1;
DLL_LINKAGE std::string key(HeroCommand command);
DLL_LINKAGE bool isDoctrine(HeroCommand command);
DLL_LINKAGE bool valid(HeroCommand command);
/// Empty rules mean legacy gameplay. Unsupported or malformed nonempty rules fail closed.
DLL_LINKAGE void validateRules(const JsonNode & rules);
DLL_LINKAGE int coefficient(const JsonNode & effect, int attack, int defense);
DLL_LINKAGE std::vector<Bonus> bonuses(const JsonNode & rules, HeroCommand command, const CGHeroInstance & hero);
}
