/* UtilityActionSpellEffectPreview.cpp, part of VCMI / New Horizons; GPL v2.0 or later. */
#include "StdInc.h"
#include "UtilityActionSpellEffectPreview.h"
#include "NewHorizonsTimedUtility.h"
#include "NewHorizonsSorcery.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsTheChosen.h"
#include "NewHorizonsPurify.h"
#include "HeroSpellCastContext.h"
#include "effects/BattleForm.h"
#include "CSpell.h"
#include "../mapObjects/CGHeroInstance.h"
namespace newHorizonsMagic
{
namespace
{
SpellEffectPreview action(const std::string & label, std::optional<int32_t> amount = std::nullopt)
{
	SpellEffectPreview result{"new-horizons.spellbook.effect." + label,
		"new-horizons.spellbook.effect." + label + ".short", {}, {}, {}};
	if(amount)
		result.primary = SpellEffectPreview::Quantity{*amount, 1, "new-horizons.spellbook.unit.number"};
	return result;
}
}
std::optional<SpellEffectPreview> utilityActionSpellEffectPreview(const CGHeroInstance & hero, const CSpell & spell,
	const CBattleInfoCallback * callback, const JsonNode & rules, int32_t power, int32_t coefficient,
	int32_t warcasting, int32_t empower, bool followup)
{
	if(!rulesActive(rules) || rules["rulesetVersion"].Integer() != SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
		return std::nullopt;
	static const std::map<std::string, std::string> categorical{
		{"core:berserk", "berserk"}, {"new-horizons:realityWarp", "realityWarp"},
		{"new-horizons:confusion", "confusion"}, {"new-horizons:puppetMaster", "puppetMaster"},
		{"core:teleport", "teleport"}};
	if(const auto found = categorical.find(spell.getJsonKey()); found != categorical.end())
		return action(found->second);
	const bool extend = callback && heroCastExtendAvailable(hero, spell, *callback, std::clamp(hero.getEffectLevel(&spell), 0, 3));
	const bool echo = followup && hasMetamagicPerk(&hero, METAMAGIC_ECHOED_DURATION);
	if(spell.getJsonKey() == newHorizonsSorcery::SPELL_LOCK_SPELL)
		return action("spellLock", durationWithCastExtensions(newHorizonsSorcery::spellLockDurationBasisPoints(power,
			hero.hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.spellbinder"),
			coefficient, warcasting, empower), extend, echo));
	if(newHorizonsPurify::enabled(rules, spell.getId()))
	{
		auto result = action("purify", newHorizonsPurify::maximumSpellEffectChoices(rules, &hero, power));
		result.qualificationKey = "new-horizons.spellbook.purifyChoices";
		return result;
	}
	if(spell.getJsonKey() == newHorizonsTheChosen::SPELL_KEY)
	{
		const auto profile = newHorizonsTheChosen::rulesFor(rules);
		if(!profile)
			return SpellEffectPreview{};
		const auto nominal = newHorizonsTheChosen::nominalDuration(*profile,
			hero.getPrimSkillLevel(PrimarySkill::SPELL_POWER), spellPowerCoefficientPercent(rules, &hero, spell.getId()));
		return action("theChosen", newHorizonsTheChosen::finishDuration(*profile, &hero, spell.getId(),
			durationWithCastExtensions(nominal, extend, echo)));
	}
	if(spell.getJsonKey() == "new-horizons:polymorph")
	{
		std::optional<int32_t> duration;
		for(const auto & [name, effect] : spell.getLevelInfo(std::clamp(hero.getEffectLevel(&spell), 0, 3)).battleEffects.Struct())
			if(effect["type"].String() == "core:battleForm")
			{
				if(duration)
					return SpellEffectPreview{};
				duration = spells::effects::BattleFormEffect::durationWithExtend(
					spells::effects::BattleFormEffect::configuredDuration(effect), extend ? 1 : 0);
			}
		if(duration)
			return action("polymorph", *duration);
		return SpellEffectPreview{};
	}
	return std::nullopt;
}
}
