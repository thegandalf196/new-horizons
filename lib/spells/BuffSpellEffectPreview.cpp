/*
 * BuffSpellEffectPreview.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BuffSpellEffectPreview.h"
#include "NewHorizonsTimedBuffMagnitude.h"
#include "NewHorizonsSorceryRevision.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellAvailability.h"
#include "CSpell.h"
#include "../mapObjects/CGHeroInstance.h"
namespace newHorizonsMagic
{
SpellEffectPreview buffSpellEffectPreview(const CGHeroInstance & hero, const CSpell & spell,
	const JsonNode & rules, int32_t power, int32_t coefficient, int32_t warcasting, int32_t empower)
{
	const auto family = spellVariantBase(rules, spell.getId());
	std::string label;
	double value;
	if(family == SpellID::SLOW)
	{
		value = -newHorizonsSorceryRevision::slowBaseReduction(power, coefficient, warcasting, empower);
		value = casterEnchantValue(&hero, family, value);
		value = newHorizonsSorceryRevision::scaleSlowVariantValue(value, spellVariantPowerPercent(rules, spell.getId()));
		label = newHorizonsSorceryRevision::slowAffectsSpeed(rules) ? "slowDual" : "slow";
	}
	else
	{
		const auto type = family == SpellID::HASTE ? BonusType::STACKS_SPEED : BonusType::LUCK;
		const auto configured = configuredAdditiveTimedValue(spell, hero.getEffectLevel(&spell), type);
		if(!configured)
			return {};
		value = casterEnchantValue(&hero, family, *configured);
		label = family == SpellID::HASTE ? "haste" : "fortune";
	}
	return {"new-horizons.spellbook.effect." + label, "new-horizons.spellbook.effect." + label + ".short",
		SpellEffectPreview::Quantity{static_cast<int64_t>(value), 1, "new-horizons.spellbook.unit.number"}, {}, "new-horizons.spellbook.recipientTierSpecialty"};
}
}
