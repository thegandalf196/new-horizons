/*
 * HeroActionAdmission.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroActionAdmission.h"
#include "PossibleSpellcast.h"
#include "../../lib/spells/CSpell.h"

namespace heroActionProjection
{
Admission admitCandidate(HypotheticBattle & state, BattleSide side, const PossibleSpellcast & candidate)
{
	Admission result;
	if(candidate.command == HeroCommand::NONE)
	{
		result.spellAllowance = state.prepareHeroSpellAllowance(side, candidate.spell->getId(),
			candidate.metamagicFollowup, candidate.metamagicGrand);
		if(!result.spellAllowance)
			return result;
		if(!state.beginProjectedHeroAction(side, *result.spellAllowance))
			return result;
		result.counterspell = state.resolveProjectedCounterspell(side, candidate.spell);
		if(!result.counterspell.resolutionKnown || !result.counterspell.negated.has_value())
		{
			// The armed ward is public, but the opposing hero's mana and
			// Countermage perk may be hidden from this player's callback.
			// Do not invent either a successful or failed Counterspell result.
			return result;
		}
		result.counterspellNegated = *result.counterspell.negated;
	}
	else
	{
		result.orderAllowance = state.prepareHeroOrderAllowance(side);
		if(!result.orderAllowance)
			return result;
		result.legacyOrder = !heroCommands::isCanonicalRules(state.getHeroCommandRules());
		if(result.legacyOrder)
		{
			if(!state.beginProjectedLegacyHeroOrder(side, candidate.command,
				candidate.commandTargets, *result.orderAllowance))
				return result;
		}
		else
		{
			// Canonical snapshots remain bound before StartAction cleanup.
			result.orderEffects = state.beginProjectedHeroOrder(side, candidate.command,
				candidate.commandTargets, *result.orderAllowance);
			if(!result.orderEffects)
				return result;
		}
	}
	result.admitted = true;
	return result;
}
}
