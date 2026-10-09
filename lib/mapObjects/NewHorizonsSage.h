/*
 * NewHorizonsSage.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#pragma once
#include "../constants/EntityIdentifiers.h"
#include <optional>
#include <vector>

class CGHeroInstance;
class CGTownInstance;

namespace newHorizonsSage
{
DLL_LINKAGE bool firstGuildVisit(const CGHeroInstance & hero, const CGTownInstance & town);
/// Eligible, undisplayed catalog restricted to captured per-level Guild schools.
DLL_LINKAGE std::vector<SpellID> candidates(const CGHeroInstance & hero, const CGTownInstance & town);
/// Highest level, then stable scoped spell identity. Does not consume a visit.
DLL_LINKAGE std::optional<SpellID> selectSpell(const CGHeroInstance & hero, const CGTownInstance & town);
DLL_LINKAGE std::optional<SpellID> wisdomReveal(const CGHeroInstance & hero, const CGTownInstance & town);
DLL_LINKAGE bool learningSelected(const CGHeroInstance & hero);
}
