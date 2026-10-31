/*
 * RestorationSpellEffectPreview.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "RestorationSpellEffectPreview.h"
#include "NewHorizonsRestorationPools.h"
#include "NewHorizonsMagic.h"
#include "CSpell.h"
#include "../mapObjects/CGHeroInstance.h"
namespace newHorizonsMagic
{
std::optional<SpellEffectPreview> restorationSpellEffectPreview(const CGHeroInstance & hero, const CSpell & spell,
	const JsonNode & rules, int32_t power, int32_t coefficient, int32_t warcasting, int32_t empower)
{
	const int specialty = hero.getNonDamageSpellSpecialtyBonusPercent(spell.getId());
	std::optional<int64_t> pool;
	if(resurrectionRestorationEnabled(rules, spell.getId()))
		pool = hero.getSpellBonus(&spell, resurrectionRestorationPool(power, coefficient, warcasting, empower, specialty), nullptr);
	else if(spell.getJsonKey() == "new-horizons:guardianSpirit")
		pool = guardianSpiritHitPoints(power, coefficient, warcasting, empower, specialty,
			hero.hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.healer"),
			hero.hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.guardian"));
	if(!pool)
		return std::nullopt;
	return {SpellEffectPreview{"new-horizons.spellbook.effect.restorationPool",
		"new-horizons.spellbook.effect.restorationPool.short",
		SpellEffectPreview::Quantity{*pool, 1, "new-horizons.spellbook.unit.number"}, {},
		"new-horizons.spellbook.restorationRecipient"}};
}
}
