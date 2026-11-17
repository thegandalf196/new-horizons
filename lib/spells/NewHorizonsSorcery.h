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
inline constexpr const char * SORCERY_MAGIC_SKILL = "new-horizons:sorceryMagic";
inline constexpr const char * FOCUS_MAGIC_SPELL = "new-horizons:focusMagic";
inline constexpr const char * ARCANE_BALLISTICS_PERK = "new-horizons:sorceryMagic.countermage";

constexpr int PHANTOM_ARMY_MANA = 15;
constexpr int PHANTOM_ARMY_DURATION_ROUNDS = 2;
/// Validated timer domain, not a balance cap on authored duration extensions.
constexpr int PHANTOM_ARMY_MAX_DURATION_ROUNDS = 1000;
constexpr int PHANTOM_ARMY_BASE_INTEGRITY_PERCENT = 20;
constexpr int PHANTOM_ARMY_INTEGRITY_CAP_PERCENT = 40;
constexpr int PHANTOM_ARMY_PHYSICAL_DAMAGE_TAKEN_PERCENT = 25;
constexpr int PHANTOM_ARMY_MAGICAL_DAMAGE_TAKEN_PERCENT = 200;
constexpr int PHANTOM_ARMY_ILLUSIONIST_BONUS_PERCENT = 25;

constexpr bool phantomArmyDurationSupported(int duration)
{
	return duration >= 1
		&& duration <= PHANTOM_ARMY_MAX_DURATION_ROUNDS;
}

/// Basis points of the source stack's current aggregate health (10000 = 100%).
/// The canonical coefficient is 0.15 percentage points per Spell Power, or 15
/// basis points per point of Spell Power.
DLL_LINKAGE int32_t phantomArmyIntegrityBasisPoints(int32_t spellPower, bool illusionist = false);

/// Returns the integral Phantom Army health pool.  Rounding is down after the
/// percentage is applied, matching the engine's integer health accounting.
DLL_LINKAGE int64_t phantomArmyIntegrityWithModifiers(int64_t sourceCurrentHealth, int32_t spellPower,
	bool illusionist, int32_t coefficientBasisPoints, int32_t empowerPercent,
	int32_t specialtyPercent);
DLL_LINKAGE int64_t phantomArmyIntegrity(int64_t sourceCurrentHealth, int32_t spellPower,
	bool illusionist = false);

/// Incoming damage multipliers are expressed as percentages so the battle
/// damage path can apply the physical 25% / magical 200% split without a
/// floating-point intermediate.
DLL_LINKAGE int phantomArmyDamageTakenPercent(bool magical);

}
