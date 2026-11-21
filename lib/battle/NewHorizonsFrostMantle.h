/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../bonuses/Bonus.h"
#include <optional>

class CGHeroInstance;

/// Adelaide's Frost Mantle (captured revisedSpecialties, format 1166). A Frost Ring
/// she casts around a friendly stack cloaks it; an enemy in the centre is chilled.
/// Both are ordinary timed bonuses, so they need no battle-state fields.
namespace newHorizonsFrostMantle
{
inline constexpr const char * SCRIPT = "new-horizons:frostMantle";
inline constexpr const char * CLOAK_STACKING = "new-horizons:frostMantle.cloak";
inline constexpr const char * CHILL_STACKING = "new-horizons:frostMantle.chill";
/// The cloak: a combat-event trigger whose value is the returned damage percentage.
DLL_LINKAGE std::optional<Bonus> cloak(const CGHeroInstance * hero);
/// The chill: -X% Initiative for the captured number of rounds.
DLL_LINKAGE std::optional<Bonus> chill(const CGHeroInstance * hero);
}
