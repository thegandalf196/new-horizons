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
inline constexpr const char * SORCERY_MAGIC_SKILL = "new-horizons:sorceryMagic";
inline constexpr const char * CHRONOMANCER_PERK = "new-horizons:sorceryMagic.chronomancer";
inline constexpr const char * SPELLBINDER_PERK = "new-horizons:sorceryMagic.spellbinder";
inline constexpr const char * FOCUS_MAGIC_SPELL = "new-horizons:focusMagic";
inline constexpr const char * ARCANE_BREACH_EFFECT = "new-horizons:arcaneBreach";
inline constexpr const char * FOCUS_MAGIC_TRIGGER = "core:focusMagic";
inline constexpr const char * ARCANE_BREACH_TRIGGER = "core:arcaneBreach";

constexpr int FOCUS_MAGIC_MANA = 11;
constexpr int FOCUS_MAGIC_DURATION_ROUNDS = 3;
constexpr int ARCANE_BREACH_DURATION_ROUNDS = 2;
constexpr int ARCANE_BREACH_MAX_MARKS = 3;
constexpr int ARCANE_BREACH_BASE_BASIS_POINTS = 1000;
constexpr int ARCANE_BREACH_POWER_BASIS_POINTS = 5;
constexpr int ARCANE_BREACH_CAP_BASIS_POINTS = 2000;

/// Captured penetration of one mark: min(20%, 10% + 0.05% * Spell Power).
/// Preserve fractional percentages until the combined penetration is applied
/// to Creature Defense. Applying a mark is a separate post-hit operation.
DLL_LINKAGE int32_t arcaneBreachMarkBasisPoints(int32_t spellPower);

constexpr int PHANTOM_ARMY_MANA = 15;
constexpr int PHANTOM_ARMY_DURATION_ROUNDS = 2;
constexpr int PHANTOM_ARMY_MAX_DURATION_ROUNDS = PHANTOM_ARMY_DURATION_ROUNDS + 1;
constexpr int PHANTOM_ARMY_BASE_INTEGRITY_PERCENT = 20;
constexpr int PHANTOM_ARMY_INTEGRITY_CAP_PERCENT = 40;
constexpr int PHANTOM_ARMY_PHYSICAL_DAMAGE_TAKEN_PERCENT = 25;
constexpr int PHANTOM_ARMY_MAGICAL_DAMAGE_TAKEN_PERCENT = 200;
constexpr int PHANTOM_ARMY_ILLUSIONIST_BONUS_PERCENT = 25;

constexpr bool phantomArmyDurationSupported(int duration)
{
	return duration >= PHANTOM_ARMY_DURATION_ROUNDS
		&& duration <= PHANTOM_ARMY_MAX_DURATION_ROUNDS;
}

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
/// Power threshold at which the radius grows.  The coefficient scales only
/// the Spell Power-derived radius term; saved v1/v2 profiles pass 100%.
DLL_LINKAGE int timeStopRadius(int32_t spellPower, bool chronomancer = false);
DLL_LINKAGE int timeStopRadius(int32_t spellPower, bool chronomancer,
	int32_t coefficientPercent);

constexpr int SPELL_LOCK_MANA = 22;
constexpr int SPELL_LOCK_BASE_DURATION_CAP = 3;
constexpr int SPELL_LOCK_POWER_PER_EXTRA_ROUND = 80;
constexpr int SPELL_LOCK_SPELLBINDER_DURATION_BONUS = 1;
constexpr int SPELL_LOCK_SPELLBINDER_DURATION_CAP = 4;

/// Number of rounds for which a stack remains sealed. Spell Power is scaled
/// by the saved school coefficient and Warcasting percentage before flooring.
/// Spellbinder extends the canonical 3-round cap to 4 rounds; Echoed Duration
/// is applied separately to an eligible follow-up cast.
DLL_LINKAGE int spellLockDuration(int32_t spellPower, bool spellbinder = false,
	int32_t coefficientPercent = 100, int32_t warcastingBonusPercent = 0);

/// Like spellLockDuration, but accepts the composed School x Spellcraft
/// coefficient in basis points so fractional percentages survive until the
/// Spell Power-derived duration term is rounded.
DLL_LINKAGE int spellLockDurationBasisPoints(int32_t spellPower, bool spellbinder,
	int32_t coefficientBasisPoints, int32_t warcastingBonusPercent = 0);

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
