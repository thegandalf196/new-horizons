/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CompiledMasteryRules.h"
namespace nativeRules
{
CompiledMasteryRules::CompiledMasteryRules(const JsonNode & rules)
	: families(newHorizonsHeroes::compileMasteryFamilies(rules))
{
}
std::optional<std::array<newHorizonsHeroes::MasteryOption, 3>> CompiledMasteryRules::options(SecondarySkill skill) const
{
	const auto found = families.find(skill);
	return found == families.end() ? std::nullopt : std::optional(found->second);
}
}
