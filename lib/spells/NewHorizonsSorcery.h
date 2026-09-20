/*
 * NewHorizonsSorcery.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <cstdint>

namespace newHorizonsSorcery
{
// These identifiers are intentionally kept here, next to the canonical
// formulas.  The content and Lua layers use the same scoped spell names.
inline constexpr const char * PHANTOM_ARMY_SPELL = "new-horizons:phantomArmy";
inline constexpr const char * TIME_STOP_SPELL = "new-horizons:timeStop";
inline constexpr const char * SPELL_LOCK_SPELL = "new-horizons:spellLock";

constexpr int PHANTOM_ARMY_MANA = 15;
constexpr int PHANTOM_ARMY_DURATION_ROUNDS = 2;
constexpr int PHANTOM_ARMY_BASE_INTEGRITY_PERCENT = 20;
constexpr int PHANTOM_ARMY_INTEGRITY_CAP_PERCENT = 40;
constexpr int PHANTOM_ARMY_PHYSICAL_DAMAGE_TAKEN_PERCENT = 25;
constexpr int PHANTOM_ARMY_MAGICAL_DAMAGE_TAKEN_PERCENT = 200;
constexpr int PHANTOM_ARMY_ILLUSIONIST_BONUS_PERCENT = 25;

/// Basis points of the source stack's current aggregate health (10000 = 100%).
/// The canonical coefficient is 0.15 percentage points per Spell Power, or 15
/// basis points per point of Spell Power.
DLL_LINKAGE int32_t phantomArmyIntegrityBasisPoints(int32_t spellPower, bool illusionist = false);

/// Returns the integral Phantom Army health pool.  Rounding is down after the
/// percentage is applied, matching the engine's integer health accounting.
DLL_LINKAGE int64_t phantomArmyIntegrity(int64_t sourceCurrentHealth, int32_t spellPower,
	bool illusionist = false);

/// Incoming damage multipliers are expressed as percentages so the battle
/// damage path can apply the physical 25% / magical 200% split without a
/// floating-point intermediate.
DLL_LINKAGE int phantomArmyDamageTakenPercent(bool magical);

constexpr int TIME_STOP_MANA = 23;
constexpr int TIME_STOP_BASE_RADIUS = 1;
constexpr int TIME_STOP_POWER_PER_EXTRA_RADIUS = 100;
constexpr int TIME_STOP_BASE_MAX_RADIUS = 2;
constexpr int TIME_STOP_CHRONOMANCER_RADIUS_BONUS = 1;

/// Radius around the selected battlefield hex.  The base spell caps at radius
/// 2; Chronomancer raises that cap to radius 3 without changing the Spell
/// Power threshold at which the radius grows.
DLL_LINKAGE int timeStopRadius(int32_t spellPower, bool chronomancer = false);

constexpr int SPELL_LOCK_MANA = 22;
constexpr int SPELL_LOCK_BASE_DURATION_CAP = 3;
constexpr int SPELL_LOCK_POWER_PER_EXTRA_ROUND = 80;
constexpr int SPELL_LOCK_SPELLBINDER_DURATION_BONUS = 1;
constexpr int SPELL_LOCK_SPELLBINDER_DURATION_CAP = 4;

/// Number of rounds for which a stack remains sealed.  Spellbinder extends the
/// canonical 3-round cap to 4 rounds.
DLL_LINKAGE int spellLockDuration(int32_t spellPower, bool spellbinder = false);

struct DLL_LINKAGE SpellLockPolicy
{
	bool removeBeneficial = false;
	bool removeHostile = false;
	bool preserveBeneficial = false;
	bool preserveHostile = false;
	bool freezeTimedEffects = true;
	bool blockFurtherMagic = true;
	bool affectsOrders = false;

	bool operator==(const SpellLockPolicy &) const = default;
};

/// Friendly targets preserve beneficial magic and clear hostile magic.  Enemy
/// targets preserve hostile magic and clear beneficial magic.  Orders remain
/// outside this policy because they are not magical effects.
DLL_LINKAGE SpellLockPolicy spellLockPolicy(bool friendlyTarget);
}
