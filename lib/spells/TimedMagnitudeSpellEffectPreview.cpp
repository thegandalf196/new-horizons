/* TimedMagnitudeSpellEffectPreview.cpp, part of VCMI / New Horizons; GPL v2.0 or later. */
#include "StdInc.h"
#include "TimedMagnitudeSpellEffectPreview.h"
#include "NewHorizonsTimedMagnitudes.h"
#include "NewHorizonsTimedBuffMagnitude.h"
#include "NewHorizonsTimedUtility.h"
#include "HeroSpellCastContext.h"
#include "NewHorizonsMagic.h"
#include "CSpell.h"
#include "../mapObjects/CGHeroInstance.h"
#include <limits>
namespace newHorizonsMagic
{
namespace
{
SpellEffectPreview quantity(const std::string & label, int64_t amount, int32_t scale = 1)
{
	return {"new-horizons.spellbook.effect." + label, "new-horizons.spellbook.effect." + label + ".short",
		SpellEffectPreview::Quantity{amount, scale, "new-horizons.spellbook.unit.number"}, {}, {}};
}
}
std::optional<SpellEffectPreview> timedMagnitudeSpellEffectPreview(const CGHeroInstance & hero, const CSpell & spell,
	const CBattleInfoCallback * callback, const JsonNode & rules, int32_t power, int32_t coefficient,
	int32_t warcasting, int32_t empower, bool followup)
{
	if(!rulesActive(rules) || rules["rulesetVersion"].Integer() != SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
		return std::nullopt;
	const int specialty = hero.getNonDamageSpellSpecialtyBonusPercent(spell.getId());
	if(spell.getJsonKey() == SHADOW_FRAILTY_SPELL)
		return quantity("frailty", frailtyDefenseLoss(power, coefficient, warcasting, empower, specialty,
			hero.hasActivePerk(std::string(SHADOW_MAGIC_SKILL), std::string(SHADOW_WITHERING_TOUCH_PERK))), 100);
	if(spell.getJsonKey() == "new-horizons:hydrasVitality")
		return quantity("hydra", hydraCapacityMillionths(power, coefficient, warcasting, empower, specialty), 1000000);
	if(spell.getJsonKey() == "new-horizons:shieldOfChaos")
	{
		auto result = quantity("chaosShield", shieldOfChaosReduction(power, coefficient, warcasting, empower,
			hero.hasActivePerk("new-horizons:chaosMagic", "new-horizons:chaosMagic.paradoxShield")), 100);
		result.qualificationKey = "new-horizons.spellbook.chaosShieldCap";
		return result;
	}
	if(spell.getJsonKey() == "new-horizons:divineRetribution")
	{
		const auto cap = divineRetributionCap(power, coefficient, warcasting, empower);
		auto result = quantity("divineReflection", divineRetributionPayout(std::numeric_limits<int64_t>::max(), cap,
			hero.hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.retributionist") ? 120 : 100));
		result.qualificationKey = "new-horizons.spellbook.divineReflectionDamage";
		return result;
	}
	const bool extend = callback && heroCastExtendAvailable(hero, spell, *callback, std::clamp(hero.getEffectLevel(&spell), 0, 3));
	const bool echoed = followup && hasMetamagicPerk(&hero, METAMAGIC_ECHOED_DURATION);
	if(spell.getJsonKey() == "new-horizons:entangle")
		return quantity("entangle", durationWithCastExtensions(entangleBaseDuration(power, coefficient, warcasting, empower,
			hero.hasActivePerk("new-horizons:natureMagic", "new-horizons:natureMagic.rootcaller")), extend, echoed));
	if(spell.getId() == SpellID::MISFORTUNE)
	{
		const auto chance = misfortuneChanceMultiplier(power, coefficient, warcasting, empower,
			hero.hasActivePerk("new-horizons:chaosMagic", "new-horizons:chaosMagic.misfortuneWeaver"));
		auto result = quantity("misfortune", static_cast<int64_t>(casterEnchantValue(&hero, spell.getId(), chance)), 100);
		result.secondary = SpellEffectPreview::Quantity{durationWithCastExtensions(
			misfortuneBaseDuration(power, coefficient, warcasting, empower), extend, echoed), 1, "new-horizons.spellbook.unit.number"};
		result.qualificationKey = "new-horizons.spellbook.misfortuneChance";
		return result;
	}
	return std::nullopt;
}
}
