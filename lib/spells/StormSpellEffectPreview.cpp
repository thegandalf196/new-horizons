/*
 * StormSpellEffectPreview.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "StormSpellEffectPreview.h"
#include "NewHorizonsStormOfDaggersDamage.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellAvailability.h"
#include "CSpell.h"
#include "../mapObjects/CGHeroInstance.h"
#include <limits>
namespace newHorizonsMagic
{
SpellEffectPreview stormSpellEffectPreview(const CGHeroInstance & hero, const CSpell & spell,
	const JsonNode & rules, int32_t power, int32_t directCoefficient, int32_t warcasting, int32_t empower)
{
	if(spell.getJsonKey() != STORM_OF_DAGGERS_SPELL || !rulesActive(rules)
		|| rules["rulesetVersion"].Integer() != SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		|| !spellAllowedBySavedRoster(rules, spell.getId()))
		return {};
	const auto formula = spellDirectDamage(rules, spell.getJsonKey());
	int64_t minimum = std::numeric_limits<int64_t>::max();
	int64_t maximum = std::numeric_limits<int64_t>::min();
	for(int count = 1; count <= STORM_OF_DAGGERS_MAX_TARGETS; ++count)
	{
		// Share and total have independent positive-half-up rounding. Never divide
		// an already-rounded total or enumerate battle units for a book readout.
		const auto raw = formula ? spells::stormOfDaggersRoundedDamage(formula->base, formula->powerCoefficient,
			power, hero.getEffectPowerDivisor(&spell), directCoefficient, warcasting, empower, count, false) : 0;
		const auto damage = spell.adjustRawDamage(&hero, nullptr, raw);
		minimum = std::min(minimum, damage);
		maximum = std::max(maximum, damage);
	}
	return {"new-horizons.spellbook.effect.storm", "new-horizons.spellbook.effect.storm.short",
		SpellEffectPreview::Quantity{minimum, 1, "new-horizons.spellbook.unit.number"},
		SpellEffectPreview::Quantity{maximum, 1, "new-horizons.spellbook.unit.number"}, "new-horizons.spellbook.stormCounts"};
}
}
