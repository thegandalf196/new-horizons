/*
 * DirectSpellEffectPreview.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "DirectSpellEffectPreview.h"
#include "CSpell.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsShadowMagnitudes.h"
#include "NewHorizonsImplosion.h"
#include "ConditionalSpellEffectPreview.h"
#include "../mapObjects/CGHeroInstance.h"
namespace newHorizonsMagic
{
namespace
{
// These families need dedicated target/count dispatch; a generic raw pool is
// insufficient even when a saved directDamage record happens to exist.
bool needsFamilyResolver(const CSpell & spell, const JsonNode & rules)
{
	if(newHorizonsImplosion::rulesFor(rules, spell.getId()) || earthquakeRulesEnabled(rules, spell.getId()))
		return true;
	static const std::set<std::string> families{
		"new-horizons:stormOfDaggers", "new-horizons:soulReaper", "new-horizons:handOfFate",
		"new-horizons:holyWrath", "new-horizons:vengefulVines", "new-horizons:naturesWrath"};
	return families.contains(spell.getJsonKey());
}
}
std::optional<int64_t> directSpellRawPotential(const CGHeroInstance & hero, const CSpell & spell,
	const JsonNode & rules, int32_t power, int32_t directCoefficient, int32_t warcasting, int32_t empower)
{
	const int divisor = std::max(1, hero.getEffectPowerDivisor(&spell));
	const int damageCoefficient = directCoefficient
		* (100 + spellPowerDamagePerkBonusPercent(rules, &hero, &spell)) / 100;
	const int specialty = hero.getDamageSpellSpecialtyBonusPercent(spell.getId());
	int64_t raw;
	if(const auto overrideValue = hero.getEffectValue(&spell); overrideValue != 0)
		raw = overrideValue;
	else if(const auto formula = spellDirectDamage(rules, spell.getJsonKey()))
		raw = formula->evaluateBasisPoints(power, divisor, damageCoefficient, empower, specialty, warcasting);
	else
	{
		if(!spell.getJsonKey().starts_with("core:"))
			return std::nullopt; // No generic base-to-effect assumption for custom families.
		const DirectDamageFormula fallback{spell.getLevelPower(std::clamp(hero.getEffectLevel(&spell), 0, 3)), spell.getBasePower()};
		raw = fallback.evaluateBasisPoints(power, divisor, damageCoefficient, empower, specialty, warcasting);
	}
	return std::max<int64_t>(0, raw);
}
SpellEffectPreview directSpellEffectPreview(const CGHeroInstance & hero, const CSpell & spell,
	const JsonNode & rules, int32_t power, int32_t directCoefficient, int32_t warcasting,
	int32_t empower, bool followup, int32_t numericalCoefficient, const CBattleInfoCallback * callback)
{
	if(const auto conditional = conditionalSpellEffectPreview(hero, spell, rules, power,
		directCoefficient, numericalCoefficient, warcasting, empower, callback))
		return *conditional;
	if(!spell.isDamage() || needsFamilyResolver(spell, rules))
		return {};
	const auto raw = directSpellRawPotential(hero, spell, rules, power, directCoefficient, warcasting, empower);
	if(!raw)
		return {};

	SpellEffectPreview result{"new-horizons.spellbook.effect.damage", "new-horizons.spellbook.effect.damage.short",
		SpellEffectPreview::Quantity{spell.adjustRawDamage(&hero, nullptr, *raw),
			1, "new-horizons.spellbook.unit.number"}, {}, {}};
	result.qualificationKey = "new-horizons.spellbook.beforeDefenses";
	if(followup)
		result.qualificationKey = "new-horizons.spellbook.followupBeforeTarget";
	if(spell.getJsonKey() == SHADOW_LIFE_DRAIN_SPELL)
	{
		result.labelKey = "new-horizons.spellbook.effect.lifeDrain";
		result.compactLabelKey = "new-horizons.spellbook.effect.lifeDrain.short";
		result.secondary = SpellEffectPreview::Quantity{lifeDrainHealingPercent(&hero),
			1, "new-horizons.spellbook.unit.number"};
		result.qualificationKey = followup ? "new-horizons.spellbook.lifeDrainFollowup"
			: "new-horizons.spellbook.lifeDrainHealing";
	}
	return result;
}
}
