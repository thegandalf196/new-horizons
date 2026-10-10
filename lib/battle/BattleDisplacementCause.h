/*
 * BattleDisplacementCause.h, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt
 */
#pragma once
#include <cstdint>

/// Gameplay provenance, deliberately independent of movement animation.
/// NONE retains ordinary movement, deployment, return, gating and legacy
/// magical relocation. Only an explicitly forced producer uses the other values.
enum class BattleDisplacementCause : uint8_t
{
	NONE = 0,
	NON_MAGICAL = 1,
	MAGICAL = 2
};

constexpr bool validBattleDisplacementCause(BattleDisplacementCause cause)
{
	return cause == BattleDisplacementCause::NONE
		|| cause == BattleDisplacementCause::NON_MAGICAL
		|| cause == BattleDisplacementCause::MAGICAL;
}
