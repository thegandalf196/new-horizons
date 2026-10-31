/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../bonuses/BonusEnum.h"
#include <optional>
#include <span>
#include <string>
namespace newHorizonsHeroes::detail
{
struct OrdinaryCreatureSpecialty
{
	const char * hero;
	const char * creature;
	const char * name;
	const char * coefficient;
	BonusType type;
	BonusValueType valueType;
	const char * description;
	std::optional<std::string> artwork = std::nullopt;
};
/// Internal finite authored rows; capture validation and contextual dispatch stay with their owner.
std::span<const OrdinaryCreatureSpecialty> ordinaryCreatureSpecialtyRegistry();
}
