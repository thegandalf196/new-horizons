/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CUnitState.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/NativeBonusGraphAccess.h"
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
	// Time Stop markers are never suppressed. A suppression marker still needs
	// the ordinary legacy query below, which selects before suppression stacking.
	static const CSelector markers = Selector::type()(BonusType::TIME_STOP).Or(
		Selector::type()(BonusType::NONE).And(Selector::sourceType()(BonusSource::SPELL_EFFECT)))
		.Or(Selector::type()(BonusType::CREATURE_ABILITY_SUPPRESSION));
	static const std::string cacheKey = "battle.CUnitState.timeStopMarkers";
	TConstBonusListPtr effects;
	{
		NativeBonusGraphAccess graphAccess;
		effects = getBonusesBeforeCreatureAbilitySuppression(markers, cacheKey, false);
	}
	// A dedicated marker wins before consulting any captured legacy identity.
	if(vstd::contains_if(*effects, [](const std::shared_ptr<Bonus> & bonus)
	{
		return bonus && bonus->type == BonusType::TIME_STOP;
	}))
		return true;

	// Under suppression, same-type bonuses from other sources can stack away a
	// legacy marker in the pre-suppression cache. Preserve the original query's
	// source selection and stacking order; even an unrecognized marker falls back.
	if(vstd::contains_if(*effects, [](const std::shared_ptr<Bonus> & bonus)
	{
		return bonus && bonus->type == BonusType::CREATURE_ABILITY_SUPPRESSION;
	}))
		effects = getBonuses(Selector::sourceType()(BonusSource::SPELL_EFFECT));

	// Preserve recognition of the original data-only NONE spell marker.
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
