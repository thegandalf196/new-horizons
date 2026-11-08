/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../../lib/bonuses/BonusEnum.h"
#include <string>
#include <unordered_map>

namespace bonusPresenceLegacy
{
using Declarations = std::unordered_map<BonusType, std::string>;

// Frozen pre-index carriedBonuses loop. The production caller still owns the
// same single getAllBonuses query; this reference consumes its returned sequence.
template<class Range, class TypeOf>
std::unordered_map<std::string, bool> collect(const Declarations & declarations,
	const Range & bonuses, TypeOf typeOf)
{
	std::unordered_map<std::string, bool> result;
	for(const auto & bonus : bonuses)
	{
		auto entry = declarations.find(typeOf(bonus));
		if(entry != declarations.end())
			result.try_emplace(entry->second, true);
	}
	return result;
}
}
