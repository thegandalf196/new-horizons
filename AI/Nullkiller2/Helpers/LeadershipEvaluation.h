/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../../lib/constants/EntityIdentifiers.h"
#include <cstdint>

class CGHeroInstance;
class CCreatureSet;
namespace newHorizonsHeroes { struct LeadershipModifiers; struct PerkOfferCandidate; }

namespace NK2AI::leadershipAI
{
uint64_t addPower(uint64_t left, uint64_t right);
uint64_t scalePower(uint64_t power, long double multiplier);
uint64_t scalePowerRatio(uint64_t power, uint32_t numerator, uint32_t denominator);
/// Strategic strength combines unchanged durability with reduced offensive
/// output. This is a valuation heuristic, not another combat damage formula.
uint64_t powerWithEfficiency(uint64_t unrestrictedPower, double efficiency);
uint64_t stackPower(const CGHeroInstance * hero, CreatureID creature, int64_t count,
	const CCreatureSet * composition = nullptr);
uint64_t armyStrength(const CGHeroInstance * hero, const CCreatureSet * army, int fortLevel = 0);
/// Read-only next-rank/perk valuation through the authoritative capacity rules.
float modifierGain(const CGHeroInstance & hero, const newHorizonsHeroes::LeadershipModifiers & modifiers);
float perkGain(const CGHeroInstance & hero, const newHorizonsHeroes::PerkOfferCandidate & candidate);
}
