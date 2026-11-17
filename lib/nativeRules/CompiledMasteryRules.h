/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../entities/hero/NewHorizonsMasteryRules.h"
#include <map>
namespace nativeRules
{
class DLL_LINKAGE CompiledMasteryRules final
{
	std::map<SecondarySkill, std::array<newHorizonsHeroes::MasteryOption, 3>> families;
public:
	explicit CompiledMasteryRules(const JsonNode &);
	std::optional<std::array<newHorizonsHeroes::MasteryOption, 3>> options(SecondarySkill) const;
};
}
