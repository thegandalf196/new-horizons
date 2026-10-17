/*
 * BattleUnitsChangedInput.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
struct BattleUnitsChanged;

/// Exact caller-input freshness only, not gameplay validity or wire encoding.
/// Includes JSON type/key order/value bits and serialized per-node metadata.
bool sameBattleUnitsChangedInput(const BattleUnitsChanged & first,
	const BattleUnitsChanged & second) noexcept;
