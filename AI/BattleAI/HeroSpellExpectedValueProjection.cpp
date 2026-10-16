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
#include <variant>

namespace
{
// Nonowning alternatives: retain the original field's float until acceptance succeeds.
struct HandOfFateExpectation
{
	const float & value;
};
struct BattleFormExpectation
{
	const float & value;
};
struct ConfusionExpectation
{
	const float & value;
};
struct PuppetMasterExpectation
{
	const float & value;
};
using ExpectedValueView = std::variant<HandOfFateExpectation, BattleFormExpectation,
	ConfusionExpectation, PuppetMasterExpectation>;

std::optional<ExpectedValueView> expectedValueView(const PossibleSpellcast & ps)
{
	// Preserve precedence even for a malformed candidate with multiple populated fields.
	// Hand of Fate and Battle Form cover the full random pool, not RNGStub's one outcome.
	if(ps.command == HeroCommand::NONE && ps.spellHandOfFateExpectedValue.has_value())
		return HandOfFateExpectation{*ps.spellHandOfFateExpectedValue};
	if(ps.command == HeroCommand::NONE && ps.spellBattleFormExpectedValue.has_value())
		return BattleFormExpectation{*ps.spellBattleFormExpectedValue};
	if(ps.command == HeroCommand::NONE && ps.spellConfusionExpectedValue.has_value())
		return ConfusionExpectation{*ps.spellConfusionExpectedValue};
	// Control has no immediate HP delta; retain its precomputed activation swing.
	if(ps.command == HeroCommand::NONE && ps.spellPuppetMasterExpectedValue.has_value())
		return PuppetMasterExpectation{*ps.spellPuppetMasterExpectedValue};
	return std::nullopt;
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
		ps.value = std::visit([&baseline](const auto & value)
		{
			return baseline + value.value;
		}, *expected);
	return true;
}
