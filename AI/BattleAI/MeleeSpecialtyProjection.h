/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <cstdint>
class HypotheticBattle;
class AttackPossibility;
namespace meleeSpecialtyAI
{
bool completedPrimaryMelee(const AttackPossibility & preview);
bool complete(HypotheticBattle & branch, uint32_t actorId, bool completedPrimaryMelee);
void complete(AttackPossibility & preview);
}
