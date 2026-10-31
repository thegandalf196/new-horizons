/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <string>
class CGHeroInstance;
class JsonNode;
namespace battle { class Unit; }
namespace newHorizonsMachineSpecialties
{
inline constexpr int MASTER_GUNNER_ORDINARY_FOLLOW_UP_DAMAGE_PERCENT = 60;
// Adjust an existing legitimate Master Gunner allowance; never grant an action.
DLL_LINKAGE int masterGunnerFollowUpDamagePercent(const CGHeroInstance * hero, int ordinaryPercent);
// Only the already-rounded Siege-derived term changes; fixed bases stay intact.
DLL_LINKAGE int siegeOutput(const CGHeroInstance & hero, const JsonNode & rules, int siegeRating, const std::string & output);
DLL_LINKAGE int catapultStructuralOutput(const CGHeroInstance & hero, const battle::Unit & actor, int siegeRating, int resolvedOutput);
}
