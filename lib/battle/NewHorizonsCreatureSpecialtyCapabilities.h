/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../constants/EntityIdentifiers.h"
class Bonus;
namespace battle { class Unit; }
namespace newHorizonsCreatureSpecialtyCapabilities
{
inline constexpr int32_t STONECALLER_PROFILE_VERSION = 1;
inline constexpr int32_t STONECALLER_BASE_AMMUNITION = 12;
enum class RangedProfileKind : int32_t { Stonecaller = 1 };
/// Numeric prepared SHOOTER profile: [version, base ammunition, base ID, upgrade ID].
/// Malformed reserved profiles throw; ordinary SHOOTER bonuses return false.
DLL_LINKAGE bool isStonecallerShooterProfile(const Bonus & bonus);
/// Stable animation provenance: independent of remaining shots or action eligibility.
DLL_LINKAGE bool stonecallerRanged(const battle::Unit & unit);
}
