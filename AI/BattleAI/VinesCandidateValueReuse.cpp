/*
 * VinesCandidateValueReuse.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "VinesCandidateValueReuse.h"
#include "HeroCandidateValueContext.h"
#include "BattleExchangeVariant.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/combatScripts/ICombatEventScript.h"
#include "../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../lib/scripting/ScriptService.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include "NewHorizonsHexOfPain.h"
#include <cmath>

namespace
{
bool eventScriptsSupported(const CBattleInfoCallback & battle)
{
	// These are the two Lua event families invoked by physical forecast strikes.
	// Certify a superset of reachable bearers, including currently dead units.
	for(const auto * unit : battle.battleGetAllUnits(false))
		for(const auto & bonus : *unit->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER))
		{
			if(!bonus || bonus->source != BonusSource::SPELL_EFFECT || !bonus->parameters)
				continue;
			const bool focus = bonus->sid.toString() == newHorizonsSorcery::FOCUS_MAGIC_SPELL;
			const bool pain = bonus->sid.toString() == newHorizonsHexOfPainAI::SPELL_ID;
			if(!focus && !pain)
				continue;
			try
			{
				const auto & script = LIBRARY->scriptTypes()->getById(bonus->subtype.as<ScriptID>());
				if(script.combatEventScript && script.combatEventScript->forecastImplementation(battle)
					!= (focus ? CombatEventForecastImplementation::CANONICAL_FOCUS_MAGIC
						: CombatEventForecastImplementation::CANONICAL_HEX_OF_PAIN))
					return false;
			}
			catch(const std::exception &)
			{
				return false; // Unknown content retains the ordinary evaluation path.
			}
		}
	return true;
}
}

bool VinesCandidateValueReuse::scoringScriptsSupported(const CBattleInfoCallback & battle)
{
	const auto * calculator = LIBRARY->scriptTypes()->getDamageCalculator();
	return calculator && calculator->forecastImplementation(battle) == DamageForecastImplementation::CANONICAL
		&& eventScriptsSupported(battle);
}

void VinesCandidateValueReuse::beginCandidate(const PossibleSpellcast & candidate)
{
	if(candidate.command != HeroCommand::NONE || !candidate.spell
		|| candidate.spell->getJsonKey() != newHorizonsVengefulVines::SPELL_KEY)
		entries.clear();
}

bool VinesCandidateValueReuse::supported(const PossibleSpellcast & candidate,
	const heroActionProjection::Admission & admission,
	const std::optional<newHorizonsVengefulVines::ProjectionReceipt> & receipt) const
{
	return (!control || control->enabled) && receipt && admission.admitted
		&& admission.spellAllowance && !admission.counterspellNegated
		&& candidate.command == HeroCommand::NONE && candidate.spell
		&& candidate.spell->getJsonKey() == newHorizonsVengefulVines::SPELL_KEY
		&& newHorizonsVengefulVines::footprint(candidate.dest).size() == 3;
}

std::optional<float> VinesCandidateValueReuse::find(const PossibleSpellcast & candidate,
	const heroActionProjection::Admission & admission,
	const std::vector<uint32_t> & sharedPurposeRecipients,
	const std::optional<newHorizonsVengefulVines::ProjectionReceipt> & receipt)
{
	if(!supported(candidate, admission, receipt))
	{
		entries.clear();
		return std::nullopt;
	}
	for(const auto & entry : entries)
		if(entry.allowance == *admission.spellAllowance
			&& heroCandidateValueContext::sameCounterspell(entry.counterspell, admission.counterspell)
			&& entry.sharedPurposeRecipients == sharedPurposeRecipients
			&& entry.receipt == *receipt && heroCandidateValueContext::sameOptions(entry.candidate, candidate))
		{
			if(control)
				++control->hits;
			return entry.candidate.value; // Bit-for-bit copy, no FP regrouping.
		}
	return std::nullopt;
}

void VinesCandidateValueReuse::remember(const PossibleSpellcast & candidate,
	const heroActionProjection::Admission & admission,
	const std::vector<uint32_t> & sharedPurposeRecipients,
	const std::optional<newHorizonsVengefulVines::ProjectionReceipt> & receipt)
{
	if(!supported(candidate, admission, receipt) || !std::isfinite(candidate.value)
		|| candidate.value == std::numeric_limits<float>::lowest()
		|| candidate.value == static_cast<float>(EvaluationResult::INEFFECTIVE_SCORE))
		return;
	entries.push_back({candidate, *admission.spellAllowance, admission.counterspell,
		sharedPurposeRecipients, *receipt});
}
