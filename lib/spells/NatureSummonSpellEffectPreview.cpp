/*
 * NatureSummonSpellEffectPreview.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NatureSummonSpellEffectPreview.h"
#include "NewHorizonsTrollSummonPower.h"
#include "NewHorizonsElementalConvergencePower.h"
#include "CSpell.h"
#include "../mapObjects/CGHeroInstance.h"
namespace newHorizonsMagic
{
std::optional<SpellEffectPreview> natureSummonSpellEffectPreview(const CGHeroInstance & hero, const CSpell & spell,
	int32_t power, int32_t coefficient, int32_t warcasting, int32_t empower)
{
	int64_t pool;
	if(spell.getJsonKey() == "new-horizons:summonTrolls")
		pool = trollSummonHitPoints(power, coefficient, warcasting, empower,
			hero.hasActivePerk("new-horizons:natureMagic", "new-horizons:natureMagic.beastcaller"));
	else if(spell.getJsonKey() == "new-horizons:elementalConvergence")
		pool = elementalConvergenceHitPoints(power, coefficient, warcasting, empower,
			hero.hasActivePerk("new-horizons:natureMagic", "new-horizons:natureMagic.elementalConjurer"));
	else
		return std::nullopt;
	return SpellEffectPreview{"new-horizons.spellbook.effect.natureSummon", "new-horizons.spellbook.effect.natureSummon.short",
		SpellEffectPreview::Quantity{pool, 1, "new-horizons.spellbook.unit.number"}, {}, "new-horizons.spellbook.summonPlacement"};
}
}
