/*
 * HeroSpellExpectedValueProjection.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroSpellExpectedValueProjection.h"
#include "PossibleSpellcast.h"
#include "../../lib/spells/CSpell.h"

#include <limits>

namespace
{
// Borrow the original field; read its float only after acceptance succeeds.
const float * expectedValueView(const PossibleSpellcast & ps)
{
	// Preserve precedence even for a malformed candidate with multiple populated fields.
	// Hand of Fate and Battle Form cover the full random pool, not RNGStub's one outcome.
	if(ps.command == HeroCommand::NONE && ps.spellHandOfFateExpectedValue.has_value())
		return &*ps.spellHandOfFateExpectedValue;
	if(ps.command == HeroCommand::NONE && ps.spellBattleFormExpectedValue.has_value())
		return &*ps.spellBattleFormExpectedValue;
	if(ps.command == HeroCommand::NONE && ps.spellConfusionExpectedValue.has_value())
		return &*ps.spellConfusionExpectedValue;
	// Control has no immediate HP delta; retain its precomputed activation swing.
	if(ps.command == HeroCommand::NONE && ps.spellPuppetMasterExpectedValue.has_value())
		return &*ps.spellPuppetMasterExpectedValue;
	return nullptr;
}
}

bool heroSpellProjection::projectPrecomputedValue(PossibleSpellcast & ps, HypotheticBattle & state,
	BattleSide side, const float & baseline, const HypotheticBattle::ProjectedCounterspellOutcome & counterspell,
	bool counterspellNegated, const std::optional<HypotheticBattle::ProjectedSpellAllowance> & spellAllowance,
	const std::vector<uint32_t> & sharedPurposeSpellRecipients, uint32_t & targetId)
{
	const auto expected = expectedValueView(ps);
	if(!expected)
		return false;
	if(ps.dest.size() != 1 || !ps.dest.front().unitValue)
	{
		ps.value = std::numeric_limits<float>::lowest();
		return true;
	}
	targetId = ps.dest.front().unitValue->unitId();
	if(counterspellNegated
		|| !state.projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
			ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
			counterspellNegated, *spellAllowance, sharedPurposeSpellRecipients))
		ps.value = std::numeric_limits<float>::lowest();
	else
		ps.value = baseline + *expected;
	return true;
}
