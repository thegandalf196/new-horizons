/*
 * HeroPurifyProjection.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "HypotheticBattle.h"

#include <cstdint>
#include <optional>
#include <vector>

class CGHeroInstance;
class PossibleSpellcast;

namespace heroSpellProjection
{
/// Projects an already selected Purify candidate and assigns its terminal score.
/// The caller retains canonical-spell/positive-value admission and terminal routing.
void projectPurifySelection(PossibleSpellcast & ps, HypotheticBattle & state,
	const CGHeroInstance * hero, BattleSide side, const float & baseline,
	const HypotheticBattle::ProjectedCounterspellOutcome & counterspell, bool counterspellNegated,
	const std::optional<HypotheticBattle::ProjectedSpellAllowance> & spellAllowance,
	const std::vector<uint32_t> & sharedPurposeSpellRecipients, uint32_t targetId);
}
