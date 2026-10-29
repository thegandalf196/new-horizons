/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include <cstdint>
#include <vector>
#include <utility>
namespace battle { class CUnitState; }
void projectVampirismHealing(battle::CUnitState * attacker, int64_t actualDamage,
 std::vector<std::pair<uint32_t, int64_t>> & healingByUnit);
