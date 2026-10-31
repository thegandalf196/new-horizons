/*
 * SpellEffectPreview.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "SpellEffectPreview.h"
#include "HeroSpellCastContext.h"
#include "CSpell.h"
#include "ISpellMechanics.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsCureHealing.h"
#include "NewHorizonsBlink.h"
#include "TimedSpellEffectPreview.h"
#include "AfflictionSpellEffectPreview.h"
#include "DirectSpellEffectPreview.h"
#include "UtilityActionSpellEffectPreview.h"
#include "KnownUtilitySpellEffectPreview.h"
#include "NewHorizonsTimedUtility.h"
#include "StormSpellEffectPreview.h"
#include "NewHorizonsSpellAvailability.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../entities/hero/NewHorizonsSchoolSpellcraft.h"
#include "../entities/hero/NewHorizonsHerbalSurgeon.h"
#include "../mapObjects/CGHeroInstance.h"

namespace newHorizonsMagic
{
namespace
{
constexpr auto UNIT = "new-horizons.spellbook.unit.number";
SpellEffectPreview quantity(const std::string & label, int64_t value, int32_t scale = 1)
{
	return {"new-horizons.spellbook.effect." + label, "new-horizons.spellbook.effect." + label + ".short",
		SpellEffectPreview::Quantity{value, scale, UNIT}, {}, "new-horizons.spellbook.targetUnknown"};
}
// BaseMechanics captures Bless duration before follow-up raw-power bonuses.
int initialSpellPower(const CGHeroInstance & hero, const CSpell & spell, const CBattleInfoCallback * callback)
{
	const int power = std::max(0, hero.getEffectPower(&spell));
	return power + (callback ? brimstoneSpellPowerBonus(hero, *callback) : 0);
}



}

static SpellEffectPreview resolveSpellEffectPreview(const CGHeroInstance & hero, const CSpell & spell,
	const CBattleInfoCallback * callback, bool followup)
{
	const auto * battle = callback ? callback->getBattle() : nullptr;
	const auto & rules = battle ? battle->getMagicRules() : hero.getMagicRules();
	if(rulesActive(rules) && !spellAllowedBySavedRoster(rules, spell.getId()))
		return {};
	const auto context = callback ? heroSpellCastContext(hero, spell, *callback, followup) : HeroSpellCastContext{};
	const int durationPower = initialSpellPower(hero, spell, callback);
	int power = durationPower;
	if(followup && callback)
		power = power * (100 + metamagicPowerBonus(hero, spell, *callback)) / 100;
	const bool v3 = rulesActive(rules) && rules["rulesetVersion"].Integer() >= SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION;
	if(!v3 && hasStormcallerPerk(&hero, &spell))
		power = power * 115 / 100;
	const auto captured = newHorizonsHeroes::captureSpellcraftContext(rules, &hero, spell.getId(), true);
	const auto coefficient = [&](newHorizonsHeroes::SpellcraftComponent component)
	{
		if(captured.factors)
			return newHorizonsHeroes::composeSpellcraftCoefficient(*captured.factors, component, context.componentBonusPercent());
		return ordinarySpellPowerCoefficientBasisPoints(rules, &hero, spell.getId(), context.componentBonusPercent());
	};
	const int numerical = coefficient(newHorizonsHeroes::SpellcraftComponent::NUMERICAL);
	const int empower = empowerSpellBonusPercent(rules, &hero, spell.getId());
	if(v3 && hasTimedSpellEffectPreview(spell, rules))
		return timedSpellEffectPreview(hero, spell, callback, followup,
			spellVariantBase(rules, spell.getId()) == SpellID::BLESS ? durationPower : power,
			numerical, context.warcasting, empower);
	if(v3 && (spell.getJsonKey() == "new-horizons:hexOfPain" || spell.getJsonKey() == "new-horizons:plague"))
		return afflictionSpellEffectPreview(hero, spell, power,
			coefficient(newHorizonsHeroes::SpellcraftComponent::DAMAGE), context.warcasting, empower);
	if(v3 && spell.getJsonKey() == STORM_OF_DAGGERS_SPELL)
		return stormSpellEffectPreview(hero, spell, rules, power,
			coefficient(newHorizonsHeroes::SpellcraftComponent::DIRECT_DAMAGE), context.warcasting, empower);
	if(v3 && isLandMine(spell.getId()))
	{
		auto result = directSpellEffectPreview(hero, spell, rules, power,
			coefficient(newHorizonsHeroes::SpellcraftComponent::DIRECT_DAMAGE), context.warcasting, empower, followup, numerical, callback);
		result.secondary = SpellEffectPreview::Quantity{landMineCountForHero(power, numerical, &hero), 1, UNIT};
		result.labelKey = "new-horizons.spellbook.effect.mines";
		result.compactLabelKey = "new-horizons.spellbook.effect.mines.short";
		result.qualificationKey = "new-horizons.spellbook.minePlacement";
		return result;
	}
	if(v3)
		if(const auto known = knownUtilitySpellEffectPreview(hero, spell, callback, rules, power, numerical,
			context.warcasting, empower, context.componentBonusPercent()))
			return *known;
	if(cureEnabled(rules, spell.getId()))
		return quantity("healing", hero.getSpellBonus(&spell,
			cureHealingBudget(&hero, power, numerical, context.warcasting, empower), nullptr));
	if(physicalPoisonEnabled(rules, spell.getId()))
	{
		auto result = quantity("poison", poisonBaseDamageBasisPoints(power, numerical, empower, poisonBaseBonusPercent(&hero)));
		result.qualificationKey = "new-horizons.spellbook.poisonTicks";
		return result;
	}
	const auto baseID = spellVariantBase(rules, spell.getId());
	const auto * family = baseID.hasValue() ? baseID.toSpell() : &spell;
	if(v3 && family && family->getJsonKey() == NATURE_REGENERATION_SPELL)
	{
		const bool herbalist = hero.hasActivePerk(std::string(NATURE_MAGIC_SKILL), std::string(NATURE_HERBALIST));
		const int specialty = newHorizonsHeroes::herbalSurgeonRegenerationPercent(&hero);
		// The shared rate stores millionths of suffered surviving-creature damage.
		auto result = quantity("regeneration", regenerationRateMillionthsBasisPointsWithSpecialty(power, numerical,
			herbalist, context.warcasting, empower, specialty), 10000);
		result.qualificationKey = "new-horizons.spellbook.regenerationRecovery";
		return result;
	}
	if(v3 && spell.getJsonKey() == newHorizonsBlink::SPELL_ID)
		return quantity("blink", newHorizonsBlink::radiusFor(power, numerical, context.warcasting));
	if(v3 && spell.getJsonKey() == "new-horizons:sanctuary")
		return {"new-horizons.spellbook.effect.sanctuary", {}, {}, {}, {}};
	if(spell.getId() == SpellID::DISPEL)
		return {"new-horizons.spellbook.effect.dispel", {}, {}, {}, {}};
	if(const auto utility = utilityActionSpellEffectPreview(hero, spell, callback, rules, power, numerical, context.warcasting, empower, followup))
		return *utility;
	return directSpellEffectPreview(hero, spell, rules, power,
		coefficient(newHorizonsHeroes::SpellcraftComponent::DIRECT_DAMAGE), context.warcasting, empower, followup, numerical, callback);
}
SpellEffectPreview spellEffectPreview(const CGHeroInstance & hero, const CSpell & spell,
	const CBattleInfoCallback * callback, bool followup)
{
	auto result = resolveSpellEffectPreview(hero, spell, callback, followup);
	if(!result.compactLabelKey.empty())
		result.narrowLabelKey = result.compactLabelKey + ".narrow";
	return result;
}

}
