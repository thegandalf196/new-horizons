/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "Destination.h"
#include "../bonuses/Bonus.h"
#include <optional>

class CGHeroInstance;
class CBattleInfoCallback;
namespace battle { class Unit; }
namespace spells { class Mechanics; }

/// Adelaide's Frost Mantle (captured revisedSpecialties, format 1166). A Frost Ring
/// she casts around a friendly stack cloaks it; an enemy in the centre is chilled.
/// Both are ordinary timed bonuses, so they need no battle-state fields.
namespace newHorizonsFrostMantle
{
inline constexpr const char * SCRIPT = "new-horizons:frostMantle";
inline constexpr const char * CLOAK_STACKING = "new-horizons:frostMantle.cloak";
inline constexpr const char * CHILL_STACKING = "new-horizons:frostMantle.chill";
/// Selected NHART status icons for the two bonus cards (44px, transparent).
inline constexpr const char * CLOAK_ICON = "NH_status_frost_mantle_cloak";
inline constexpr const char * CHILL_ICON = "NH_status_frost_mantle_chill";
/// The cloak: a combat-event trigger whose value is the returned damage percentage.
DLL_LINKAGE std::optional<Bonus> cloak(const CGHeroInstance * hero);
/// The chill: -X% Initiative for the captured number of rounds.
DLL_LINKAGE std::optional<Bonus> chill(const CGHeroInstance * hero);
/// The chill reaches an enemy centre only if Frost Ring's ordinary receptivity
/// admits it (spell, level and school immunity, as for any ring target).
DLL_LINKAGE bool chills(const spells::Mechanics & mechanics, const battle::Unit & centre);
/// The same check for the hero's plain Frost Ring (battle AI valuation).
DLL_LINKAGE bool chills(const CBattleInfoCallback & battle, const CGHeroInstance & hero, const battle::Unit & centre);
/// The holder's Frost Ring centred on a living stack is legal for the cloak or
/// receptive chill alone, even when the ring reaches no other stack (server and client).
DLL_LINKAGE bool centredOnStack(const spells::Mechanics & mechanics, const battle::Target & target);
}
