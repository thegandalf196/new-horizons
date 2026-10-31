/*
 * AfflictionSpellEffectPreview.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "AfflictionSpellEffectPreview.h"
#include "NewHorizonsTimedUtility.h"
#include "NewHorizonsMagic.h"
#include "CSpell.h"
#include "../mapObjects/CGHeroInstance.h"
namespace newHorizonsMagic
{
SpellEffectPreview afflictionSpellEffectPreview(const CGHeroInstance & hero, const CSpell & spell,
	int32_t power, int32_t damageCoefficient, int32_t warcasting, int32_t empower)
{
	const auto specialty = hero.getDamageSpellSpecialtyBonusPercent(spell.getId());
	if(spell.getJsonKey() == "new-horizons:hexOfPain")
	{
		const bool painweaver = hero.hasActivePerk("new-horizons:shadowMagic", "new-horizons:shadowMagic.painweaver");
		return {"new-horizons.spellbook.effect.hex", "new-horizons.spellbook.effect.hex.short",
			SpellEffectPreview::Quantity{hexOfPainFlatDamage(power, damageCoefficient, warcasting, empower, specialty, painweaver),
				1, "new-horizons.spellbook.unit.number"},
			SpellEffectPreview::Quantity{hexOfPainDamageSharePercent(), 1, "new-horizons.spellbook.unit.number"},
			"new-horizons.spellbook.hexTrigger"};
	}
	if(spell.getJsonKey() == "new-horizons:plague")
		return {"new-horizons.spellbook.effect.plague", "new-horizons.spellbook.effect.plague.short",
			SpellEffectPreview::Quantity{plagueTickDamage(power, damageCoefficient, specialty),
				1, "new-horizons.spellbook.unit.number"}, {}, "new-horizons.spellbook.plagueSnapshot"};
	return {};
}
}
