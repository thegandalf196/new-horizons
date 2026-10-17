/*
 * HeroActionAdmission.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "HypotheticBattle.h"
#include "../../lib/battle/HeroOrderEffects.h"

class PossibleSpellcast;

namespace heroActionProjection
{
struct Admission
{
	bool admitted = false;
	bool legacyOrder = false;
	std::optional<HypotheticBattle::ProjectedSpellAllowance> spellAllowance;
	std::optional<HypotheticBattle::ProjectedOrderAllowance> orderAllowance;
	std::optional<heroCommands::PreparedOrderEffects> orderEffects;
	HypotheticBattle::ProjectedCounterspellOutcome counterspell;
	bool counterspellNegated = false;
};

/// For an already generated candidate, prepare and begin its private action before
/// resolving the opposing ward. A refusal can retain a begun private action; the
/// caller must discard that candidate branch, not continue its valuation or commit.
/// This is neither command validation nor an authoritative action/mana mutation.
Admission admitCandidate(HypotheticBattle & state, BattleSide side, const PossibleSpellcast & candidate);
}
