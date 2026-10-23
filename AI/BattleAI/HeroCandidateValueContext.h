/*
 * HeroCandidateValueContext.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "HeroActionAdmission.h"
class PossibleSpellcast;
namespace heroCandidateValueContext
{
/// Every cast option except original destination and finalized value; no FP tolerance.
bool sameOptions(const PossibleSpellcast & a, const PossibleSpellcast & b);
bool sameCounterspell(const HypotheticBattle::ProjectedCounterspellOutcome & a,
	const HypotheticBattle::ProjectedCounterspellOutcome & b);
}
