/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/entities/hero/NewHorizonsStartingPerks.h"
#include "../../lib/entities/hero/HeroStartingProjection.h"
namespace perkInitializationReference
{
void initialize(newHorizonsHeroes::PerkState &, const JsonNode &, HeroTypeID,
 const std::vector<newHorizonsHeroes::PerkSelection> &, const std::vector<newHorizonsHeroes::PerkSelection> &,
 const std::function<int(const std::string &)> &);
}
