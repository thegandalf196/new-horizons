/*
 * PrimarySkillPolicy.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../constants/EntityIdentifiers.h"
#include <array>

class BonusList;
class JsonNode;

namespace primarySkillPolicy
{
struct DLL_LINKAGE LegacyBounds
{
	std::array<int, 4> minimum;
	std::array<int, 4> maximum;
	int value(PrimarySkill skill, int rawValue) const;
};

DLL_LINKAGE LegacyBounds legacyBounds();
DLL_LINKAGE int growthValue(int rawValue, const JsonNode & rules);
/// Explicit PRIMARY_SKILL list; no native hero values or cache are consulted.
DLL_LINKAGE int projectedValue(PrimarySkill skill, const BonusList & primaryBonuses, const JsonNode * growthRules);
}
