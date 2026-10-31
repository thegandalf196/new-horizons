/*
 * TimedSpellEffectPreview.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "TimedSpellEffectPreview.h"
#include "TimedMagnitudeSpellEffectPreview.h"
#include "NewHorizonsTimedUtility.h"
#include "NewHorizonsBlessDuration.h"
#include "NewHorizonsSpellAvailability.h"
#include "NewHorizonsSorcery.h"
#include "HeroSpellCastContext.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellcraft.h"
#include "CSpell.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../mapObjects/CGHeroInstance.h"
namespace newHorizonsMagic
{
bool hasTimedSpellEffectPreview(const CSpell & spell, const JsonNode & rules)
{
	static const std::set<std::string> families{"new-horizons:frailty", "new-horizons:hydrasVitality",
		"new-horizons:shieldOfChaos", "new-horizons:divineRetribution", "new-horizons:entangle",
		"new-horizons:holyArmor", "new-horizons:heavenlyGale", "new-horizons:timeStop"};
	return families.contains(spell.getJsonKey()) || spell.getId() == SpellID::MISFORTUNE
		|| spell.getId() == SpellID::FORGETFULNESS || spellVariantBase(rules, spell.getId()) == SpellID::BLESS;
}
SpellEffectPreview timedSpellEffectPreview(const CGHeroInstance & hero, const CSpell & spell,
	const CBattleInfoCallback * callback, bool followup, int32_t power, int32_t coefficient,
	int32_t warcasting, int32_t empower)
{
	const auto & rules = callback && callback->getBattle() ? callback->getBattle()->getMagicRules() : hero.getMagicRules();
	if(const auto magnitude = timedMagnitudeSpellEffectPreview(hero, spell, callback, rules, power, coefficient, warcasting, empower, followup))
		return *magnitude;
	if(spellVariantBase(rules, spell.getId()) == SpellID::BLESS)
	{
		const auto ordinary = blessOrdinaryDuration(power, coefficient, warcasting, empower,
			hero.getNonDamageSpellSpecialtyBonusPercent(SpellID::BLESS));
		const auto duration = durationWithCastExtensions(blessCasterDuration(hero, ordinary,
			callback && brimstoneSpellPowerBonus(hero, *callback) != 0),
			callback && heroCastExtendAvailable(hero, spell, *callback, std::clamp(hero.getEffectLevel(&spell), 0, 3)),
			followup && hasMetamagicPerk(&hero, METAMAGIC_ECHOED_DURATION));
		return {"new-horizons.spellbook.effect.bless", "new-horizons.spellbook.effect.bless.short",
			SpellEffectPreview::Quantity{duration, 1, "new-horizons.spellbook.unit.number"}, {}, {}};
	}
	if(spell.getJsonKey() == "new-horizons:heavenlyGale")
	{
		return {"new-horizons.spellbook.effect.heavenlyGale", "new-horizons.spellbook.effect.heavenlyGale.short",
			SpellEffectPreview::Quantity{heavenlyGaleReductionBasisPoints(power, coefficient, warcasting, empower,
				hero.hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.aegis")),
				100, "new-horizons.spellbook.unit.number"}, {}, "new-horizons.spellbook.heavenlyGaleRecipient"};
	}
	if(spell.getJsonKey() == "new-horizons:holyArmor")
	{
		const auto base = configuredHolyArmorBase(spell, hero.getEffectLevel(&spell));
		if(!base)
			return {};
		const bool aegis = hero.hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.aegis");
		return {"new-horizons.spellbook.effect.holyArmor", "new-horizons.spellbook.effect.holyArmor.short",
			SpellEffectPreview::Quantity{holyArmorReduction(*base, power, coefficient, warcasting, empower, aegis),
				1, "new-horizons.spellbook.unit.number"}, {}, "new-horizons.spellbook.holyArmorRecipient"};
	}
	if(spell.getJsonKey() == newHorizonsSorcery::TIME_STOP_SPELL)
	{
		const bool chronomancer = hero.hasActivePerk(newHorizonsSorcery::SORCERY_MAGIC_SKILL, newHorizonsSorcery::CHRONOMANCER_PERK);
		return {"new-horizons.spellbook.effect.timeStop", "new-horizons.spellbook.effect.timeStop.short",
			SpellEffectPreview::Quantity{newHorizonsSorcery::timeStopRadius(power, coefficient, warcasting, empower, chronomancer),
				1, "new-horizons.spellbook.unit.number"}, {}, {}};
	}
	if(spell.getId() == SpellID::FORGETFULNESS)
	{
		const bool echoed = followup && hasMetamagicPerk(&hero, METAMAGIC_ECHOED_DURATION);
		const auto duration = durationWithCastExtensions(forgetfulnessBaseDuration(power, coefficient, warcasting, empower),
			(callback && heroCastExtendAvailable(hero, spell, *callback, std::clamp(hero.getEffectLevel(&spell), 0, 3))), echoed);
		return {"new-horizons.spellbook.effect.forgetfulness", "new-horizons.spellbook.effect.forgetfulness.short",
			SpellEffectPreview::Quantity{duration, 1, "new-horizons.spellbook.unit.number"}, {}, {}};
	}
	return {};
}
}
