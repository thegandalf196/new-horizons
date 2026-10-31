/*
 * ConditionalSpellEffectPreview.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ConditionalSpellEffectPreview.h"
#include "DirectSpellEffectPreview.h"
#include "PlacementSpellEffectPreview.h"
#include "NewHorizonsTimedBuffMagnitude.h"
#include "NewHorizonsLightDamage.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsImplosion.h"
#include "NewHorizonsNaturesWrath.h"
#include "NewHorizonsPandemonium.h"
#include "../battle/NewHorizonsShadowGift.h"
#include "CSpell.h"
#include "../mapObjects/CGHeroInstance.h"
namespace newHorizonsMagic
{
namespace
{
SpellEffectPreview value(const std::string & label, int64_t first, int32_t scale,
	const std::string & qualifier, std::optional<int64_t> second = std::nullopt)
{
	SpellEffectPreview result{"new-horizons.spellbook.effect." + label,
		"new-horizons.spellbook.effect." + label + ".short",
		SpellEffectPreview::Quantity{first, scale, "new-horizons.spellbook.unit.number"}, {},
		qualifier};
	if(second)
		result.secondary = SpellEffectPreview::Quantity{*second, scale, "new-horizons.spellbook.unit.number"};
	return result;
}
}
std::optional<SpellEffectPreview> conditionalSpellEffectPreview(const CGHeroInstance & hero, const CSpell & spell,
	const JsonNode & rules, int32_t power, int32_t directCoefficient, int32_t numericalCoefficient,
	int32_t warcasting, int32_t empower, const CBattleInfoCallback * callback)
{
	if(!rulesActive(rules) || rules["rulesetVersion"].Integer() != SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
		return std::nullopt;
	if(const auto placed = placementSpellEffectPreview(hero, spell, callback, rules, power,
		directCoefficient, numericalCoefficient, warcasting, empower))
		return *placed;
	if(spell.getJsonKey() == newHorizonsShadowGift::SPELL_ID)
		return value("shadowGift", newHorizonsShadowGift::getDamageBonusBasisPoints(10, power,
			numericalCoefficient, warcasting, empower), 100, "new-horizons.spellbook.shadowGiftChoice",
			newHorizonsShadowGift::getDamageBonusBasisPoints(30, power, numericalCoefficient, warcasting, empower));
	if(const auto profile = newHorizonsImplosion::rulesFor(rules, spell.getId()))
		return value("implosion", spell.adjustRawDamage(&hero, nullptr, newHorizonsImplosion::damage(
			*profile, 10000, power, directCoefficient, warcasting, empower)), 100, "new-horizons.spellbook.healthRate");
	if(spell.getJsonKey() == newHorizonsNaturesWrath::SPELL_KEY)
	{
		const auto raw = newHorizonsNaturesWrath::hopPower(power, directCoefficient, warcasting, empower,
			newHorizonsNaturesWrath::hasWorldroot(&hero), 0);
		return value("naturesWrath", spell.adjustRawDamage(&hero, nullptr, raw), 1, "new-horizons.spellbook.naturesWrathRoute",
			hero.getSpellBonus(&spell, raw, nullptr));
	}
	if(spell.getJsonKey() == newHorizonsPandemonium::SPELL_KEY)
		return value("pandemonium", spell.adjustRawDamage(&hero, nullptr, newHorizonsPandemonium::rawPower(
			power, numericalCoefficient, warcasting, empower, newHorizonsPandemonium::hasMaster(&hero), 1)),
			1, "new-horizons.spellbook.pandemoniumCount");
	if(spell.getJsonKey() != SHADOW_SOUL_REAPER_SPELL && spell.getJsonKey() != "new-horizons:handOfFate"
		&& spell.getJsonKey() != "new-horizons:holyWrath" && spell.getJsonKey() != "new-horizons:vengefulVines")
		return std::nullopt;
	const auto raw = directSpellRawPotential(hero, spell, rules, power, directCoefficient, warcasting, empower);
	if(!raw)
		return SpellEffectPreview{};
	const auto ordinary = spell.adjustRawDamage(&hero, nullptr, *raw);
	if(spell.getJsonKey() == "new-horizons:vengefulVines")
	{
		auto result = value("damage", ordinary, 1, "new-horizons.spellbook.vinesShape");
		if(const auto movement = configuredAdditiveTimedValue(spell, hero.getEffectLevel(&spell), BonusType::STACKS_MOVEMENT_RANGE))
		{
			result.labelKey = "new-horizons.spellbook.effect.vines";
			result.compactLabelKey = "new-horizons.spellbook.effect.vines.short";
			result.secondary = SpellEffectPreview::Quantity{static_cast<int64_t>(*movement), 1, "new-horizons.spellbook.unit.number"};
		}
		return result;
	}
	if(spell.getJsonKey() == "new-horizons:holyWrath")
		return value("holyWrath", ordinary, 1, "new-horizons.spellbook.holyWrathRecipient",
			spell.adjustRawDamage(&hero, nullptr, *raw, 0, 0, holyWrathRecipientMultiplier(rules, spell.getId(), true)));
	if(spell.getJsonKey() == "new-horizons:handOfFate")
		return value("handOfFate", ordinary, 1, "new-horizons.spellbook.handOfFateSpill");
	const auto missing = soulReaperMissingHealthDamage(rules, spell.getId(), 10000, 0);
	auto result = value("soulReaper", ordinary, 1, "new-horizons.spellbook.soulReaperHealth");
	if(missing)
		result.secondary = SpellEffectPreview::Quantity{spell.adjustRawDamage(&hero, nullptr, *missing), 100,
			"new-horizons.spellbook.unit.number"};
	return result;
}
}
