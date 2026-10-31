/* PlacementSpellEffectPreview.cpp, part of VCMI / New Horizons; GPL v2.0 or later. */
#include "StdInc.h"
#include "PlacementSpellEffectPreview.h"
#include "NewHorizonsPlacementPower.h"
#include "NewHorizonsMagic.h"
#include "CSpell.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../battle/CBattleInfoCallback.h"
namespace newHorizonsMagic
{
namespace
{
SpellEffectPreview quantity(const std::string & label, int64_t first, const std::string & qualifier,
	std::optional<int64_t> second = std::nullopt)
{
	SpellEffectPreview result{"new-horizons.spellbook.effect." + label,
		"new-horizons.spellbook.effect." + label + ".short",
		SpellEffectPreview::Quantity{first, 1, "new-horizons.spellbook.unit.number"}, {}, qualifier};
	if(second)
		result.secondary = SpellEffectPreview::Quantity{*second, 1, "new-horizons.spellbook.unit.number"};
	return result;
}
}
std::optional<SpellEffectPreview> placementSpellEffectPreview(const CGHeroInstance & hero, const CSpell & spell,
	const CBattleInfoCallback * callback, const JsonNode & rules, int32_t power, int32_t directCoefficient,
	int32_t numericalCoefficient, int32_t warcasting, int32_t empower)
{
	if(spell.getJsonKey() == "new-horizons:verdantPrison")
		return quantity("verdant", verdantPrisonHitPoints(power, numericalCoefficient, warcasting, empower,
			hero.hasActivePerk("new-horizons:natureMagic", "new-horizons:natureMagic.verdantWarden")),
			"new-horizons.spellbook.verdantPlacement");
	if(spell.getJsonKey() == "new-horizons:transfigureMatter")
	{
		const bool shaper = hero.hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.matterShaper");
		return quantity("transfigure", transfigureHitPoints(power, numericalCoefficient, warcasting, empower, 1, shaper),
			"new-horizons.spellbook.transfigureFootprint", transfigureHitPoints(power, numericalCoefficient, warcasting, empower, 2, shaper));
	}
	if(!earthquakeRulesEnabled(rules, spell.getId()))
		return std::nullopt;
	const auto & profile = rules["spells"]["core:earthquake"]["earthquake"];
	const auto sections = earthquakeSectionCount(profile, power, numericalCoefficient, warcasting, empower);
	if(callback && callback->hasFortifications())
		return quantity("earthquakeSiege", earthquakeStructuralDamage(profile["structuralDamage"].Integer(),
			hero.hasActivePerk("new-horizons:natureMagic", "new-horizons:natureMagic.geomancer")),
			"new-horizons.spellbook.earthquakeSiege", sections);
	const int coefficient = directCoefficient * (100 + spellPowerDamagePerkBonusPercent(rules, &hero, &spell)) / 100;
	const auto overrideValue = hero.getEffectValue(&spell);
	const auto raw = overrideValue != 0 ? overrideValue : earthquakeRawDamage(profile, power, coefficient, warcasting, empower);
	return quantity("earthquakeField", spell.adjustRawDamage(&hero, nullptr, raw),
		"new-horizons.spellbook.earthquakeMode", sections);
}
}
