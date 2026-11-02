/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedLegacyTimeStopCapture.h"
#include "../bonuses/BonusParameters.h"
#include "../spells/CSpell.h"
#include "../spells/NewHorizonsSorcery.h"
#include "../GameLibrary.h"
#include "../modding/IdentifierStorage.h"
#include "../modding/ModScope.h"
#include <set>
#include <stdexcept>
#include <unordered_set>
namespace battle::ownedFrameCapture
{
OwnedUnitEnvironment::Facts::LegacyTimeStopIdentity captureLegacyTimeStopIdentities(
	const std::vector<std::shared_ptr<const Bonus>> & roots)
{
	if(!LIBRARY) throw std::logic_error("Legacy Time Stop capture requires its initialized source library");
	ownedBonusPayloads::validateTypesAndTopology(roots);
	auto raw = roots;
	std::set<SpellID> needed; std::unordered_set<const Bonus *> visited;
	for(size_t i = 0; i < raw.size(); ++i)
	{
		const auto record = raw[i];
		if(!record) throw std::invalid_argument("Null captured clock dependency record");
		if(!visited.insert(record.get()).second) continue;
		// Canonical record protection resolves identity before its marker-type filter.
		if(record->source == BonusSource::SPELL_EFFECT && record->sid.as<SpellID>().hasValue()) needed.insert(record->sid.as<SpellID>());
		if(const auto * parameters = record->parameters ? record->parameters->combatEffects() : nullptr)
			for(const auto & effect : parameters->effects)
				if(const auto * child = std::get_if<BonusParametersOnCombatEvent::CombatEffectBonus>(&effect); child && child->bonus)
					raw.push_back(child->bonus);
	}
	OwnedUnitEnvironment::Facts::LegacyTimeStopIdentity result;
	const auto registered = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(),
		SpellID::entityType(), newHorizonsSorcery::TIME_STOP_SPELL, true);
	if(registered)
	{
		const SpellID canonical(*registered);
		if(!canonical.hasValue()) throw std::invalid_argument("Malformed canonical Time Stop registry identity");
		const auto * spell = canonical.toSpell();
		if(!spell || spell->getJsonKey() != newHorizonsSorcery::TIME_STOP_SPELL)
			throw std::invalid_argument("Canonical Time Stop registry identity is inconsistent");
		result.canonical = canonical; needed.insert(canonical);
	}
	for(const auto id : needed)
	{
		// Preserve native malformed-present/out-of-range lookup failures NOW;
		// known absent canonical identity must never suppress those errors.
		const auto * spell = id.toSpell();
		const bool matches = spell && spell->getJsonKey() == newHorizonsSorcery::TIME_STOP_SPELL;
		if(matches && (!result.canonical || *result.canonical != id))
			throw std::invalid_argument("Ambiguous captured Time Stop registry identity");
		result.classified.emplace(id, matches);
	}
	return result;
}
}
