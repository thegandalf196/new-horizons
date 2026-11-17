/*
 * KnownUtilitySpellEffectPreview.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "KnownUtilitySpellEffectPreview.h"
#include "HeroSpellCastContext.h"
#include "NewHorizonsTimedUtility.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSorcery.h"
#include "NewHorizonsSorceryRevision.h"
#include "NewHorizonsShadowMagnitudes.h"
#include "NewHorizonsSpellAvailability.h"
#include "BuffSpellEffectPreview.h"
#include "NatureSummonSpellEffectPreview.h"
#include "RestorationSpellEffectPreview.h"
#include "CSpell.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../battle/NewHorizonsSoulChain.h"
namespace newHorizonsMagic
{
namespace
{
SpellEffectPreview quantity(const std::string & label, int64_t value, int32_t scale = 1)
{
	return {"new-horizons.spellbook.effect." + label, "new-horizons.spellbook.effect." + label + ".short",
		SpellEffectPreview::Quantity{value, scale, "new-horizons.spellbook.unit.number"}, {},
		"new-horizons.spellbook.targetUnknown"};
}
}
std::optional<SpellEffectPreview> knownUtilitySpellEffectPreview(const CGHeroInstance & hero, const CSpell & spell,
	const CBattleInfoCallback * callback, const JsonNode & rules, int32_t power, int32_t coefficient, int32_t empower, int32_t additionalComponentPercent)
{
	if(const auto summoned = natureSummonSpellEffectPreview(hero, spell, power, coefficient, empower))
		return summoned;
	if(const auto restored = restorationSpellEffectPreview(hero, spell, rules, power, coefficient, empower))
		return restored;
	const auto family = spellVariantBase(rules, spell.getId());
	if(family == SpellID::SLOW || family == SpellID::HASTE || family == SpellID::FORTUNE)
		return buffSpellEffectPreview(hero, spell, rules, power, coefficient, empower);
	if(spell.getJsonKey() == newHorizonsSorcery::FOCUS_MAGIC_SPELL)
	{
		if(!newHorizonsSorceryRevision::usesFocusEnchantment(rules)) return SpellEffectPreview{};
		auto result = quantity("focus", 3);
		result.secondary = SpellEffectPreview::Quantity{
			20 + newHorizonsSorceryRevision::focusAdditionalRangedPercent(&hero, spell.getId()), 1,
			"new-horizons.spellbook.unit.number"};
		result.qualificationKey = "new-horizons.spellbook.focusRecipient";
		return result;
	}
	if(spell.getJsonKey() == "new-horizons:summonLivingArmor")
	{
		const auto & profile = rules["spells"][spell.getJsonKey()]["livingArmor"];
		if(!profile.isStruct() || profile["version"].Integer() != 1)
			return SpellEffectPreview{};
		return quantity("livingArmor", newHorizonsSorceryRevision::livingArmorHitPoints(
			profile["baseHitPoints"].Integer(), profile["spellPowerCoefficient"].Integer(), power,
			std::max(1, hero.getEffectPowerDivisor(&spell)), coefficient, empower));
	}
	if(spell.getJsonKey() == SHADOW_VAMPIRISM_SPELL)
		return quantity("vampirism", vampirismHealBasisPoints(power, coefficient, empower,
			hero.hasActivePerk(std::string(SHADOW_MAGIC_SKILL), std::string(SHADOW_NIGHT_FEEDER_PERK))), 100);
	if(spell.getJsonKey() == newHorizonsSoulChain::SPELL_ID)
	{
		auto result = quantity("soulChain", newHorizonsSoulChain::echoPercentBasisPoints(
			std::max<int32_t>(0, power), coefficient,
			hero.hasActivePerk(std::string(SHADOW_MAGIC_SKILL), "new-horizons:shadowMagic.soulBinder")), 100);
		result.qualificationKey = "new-horizons.spellbook.soulChainRecipient";
		return result;
	}
	if(spell.getJsonKey() == SHADOW_DOOM_SPELL)
	{
		auto result = quantity("doom", doomCripplingPenaltyPercent(power, coefficient, empower));
		result.qualificationKey = "new-horizons.spellbook.doomRecipient";
		return result;
	}
	if(const auto count = quicksandPatchCount(rules, &hero, spell.getId(), power,
		std::max(1, hero.getEffectPowerDivisor(&spell)), empower, additionalComponentPercent, coefficient))
		return quantity("patches", *count);
	if(const auto morale = sorrowMoralePenalty(rules, &hero, spell.getId(), power, empower, additionalComponentPercent, coefficient))
		return quantity("morale", *morale);
	if(const auto duration = curseDurationRounds(rules, &hero, spell.getId()))
	{
		// The authoritative Curse path adds Extend only; it does not call the
		// general duration adjustment.
		const bool extend = callback && heroCastExtendAvailable(hero, spell, *callback,
			std::clamp(hero.getEffectLevel(&spell), 0, 3));
		return quantity("curse", *duration + static_cast<int>(extend));
	}
	if(const auto pool = reanimateBaseHealingPool(rules, &hero, spell.getId(),
		std::max(hero.getPrimSkillLevel(PrimarySkill::SPELL_POWER), 0), additionalComponentPercent, coefficient))
	{
		auto result = quantity("restoration", *pool);
		result.qualificationKey = "new-horizons.spellbook.reanimateRecipient";
		return result;
	}
	if(spell.getJsonKey() == newHorizonsSorcery::PHANTOM_ARMY_SPELL)
	{
		const bool illusionist = hero.hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.illusionist");
		auto result = quantity("phantom", newHorizonsSorcery::phantomArmyIntegrityWithModifiers(10000, power,
			illusionist, coefficient, empower, hero.getNonDamageSpellSpecialtyBonusPercent(spell.getId())), 100);
		result.qualificationKey = "new-horizons.spellbook.phantomRecipient";
		return result;
	}
	return std::nullopt;
}
}
