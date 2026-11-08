/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <cstdint>
struct DamageRange;
namespace barrageProbability
{
struct TargetWindow
{
	int64_t firstCreatureHP = 0;
	int64_t totalHealth = 0; // Includes ordinary temporary hit points.
	int64_t temporaryHitPoints = 0;
	int64_t arcaneShield = 0;
	int64_t guardianSpirit = 0;
};
/// Conservative nominal chance of a primary casualty AND surviving recipient.
/// Uses the native min(10,count) sample average, fixed pre-sequence range, at
/// most two modeled shots, and a sufficient bound for per-shot floor rounding.
/// Additional shots use guaranteed/impossible exits, or require guaranteed
/// recipient survival before the first-two-shot lower bound is meaningful.
/// No RNG, world state, branch creation, recipient search or persistent cache.
long double primaryCasualtyAndSurvival(const DamageRange &, int32_t attackerCount,
	int normalShots, const TargetWindow &);
}
