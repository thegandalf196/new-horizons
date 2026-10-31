/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <span>
class HypotheticBattle;
struct BattleStackAttacked;
namespace projectedInjuryLifecycle
{
void before(HypotheticBattle & battle, std::span<const BattleStackAttacked> hits);
void after(HypotheticBattle & battle, std::span<const BattleStackAttacked> hits);
}
