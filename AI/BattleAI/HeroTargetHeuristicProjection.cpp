/*
 * HeroTargetHeuristicProjection.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroTargetHeuristicProjection.h"
#include "PossibleSpellcast.h"
#include "../../lib/spells/CSpell.h"

#include <limits>

void heroSpellProjection::projectTargetHeuristic(PossibleSpellcast & ps,
	const float & forecast, HypotheticBattle & state, BattleSide side,
	const float & baseline, const HypotheticBattle::ProjectedCounterspellOutcome & counterspell,
	bool counterspellNegated, const std::optional<HypotheticBattle::ProjectedSpellAllowance> & spellAllowance,
	const std::vector<uint32_t> & sharedPurposeSpellRecipients, uint32_t & targetId)
{
	if(ps.dest.size() != 1 || !ps.dest.front().unitValue)
	{
		ps.value = std::numeric_limits<float>::lowest();
		return;
	}
	targetId = ps.dest.front().unitValue->unitId();
	if(counterspellNegated
		|| !state.projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
			ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
			counterspellNegated, *spellAllowance, sharedPurposeSpellRecipients))
		ps.value = std::numeric_limits<float>::lowest();
	else
		ps.value = baseline + forecast;
}
