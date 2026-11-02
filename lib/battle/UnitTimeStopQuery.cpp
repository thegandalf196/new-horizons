/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CUnitState.h"
#include "../bonuses/BonusList.h"
#include "../spells/CSpell.h"
#include "../spells/NewHorizonsSorcery.h"
namespace battle
{
std::optional<bool> CUnitState::legacyTimeStopSpellMatch(SpellID spell) const
{
	return env ? env->unitLegacyTimeStopSpellMatch(this, spell) : std::nullopt;
}
bool CUnitState::isTimeStopped() const
{
	if(hasBonusOfType(BonusType::TIME_STOP))
		return true;

	// Compatibility with the original data-only fallback marker.  New saves
	// use the dedicated TIME_STOP bonus, but an in-flight legacy battle may
	// still carry a hidden NONE marker from the Lua implementation.
	const auto effects = getBonuses(Selector::sourceType()(BonusSource::SPELL_EFFECT));
	return vstd::contains_if(*effects, [this](const std::shared_ptr<Bonus> & bonus)
	{
		if(!bonus || bonus->type != BonusType::NONE || !bonus->sid.as<SpellID>().hasValue())
			return false;
		const auto identity = bonus->sid.as<SpellID>();
		if(const auto captured = legacyTimeStopSpellMatch(identity)) return *captured;
		const auto * spell = identity.toSpell();
		return spell && spell->getJsonKey() == newHorizonsSorcery::TIME_STOP_SPELL;
	});
}

}
