/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsGenieSage.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../CCreatureHandler.h"
#include "../../GameLibrary.h"
#include "../../modding/IdentifierStorage.h"
#include "../../modding/ModScope.h"
#include "../../battle/Unit.h"
#include "../../bonuses/Bonus.h"
#include "../../bonuses/Limiters.h"
#include <string_view>
#include <tuple>
namespace newHorizonsHeroes
{
namespace
{
constexpr std::string_view prefix = "new-horizons:wheel-ability:";
constexpr std::string_view effectMarker = "new-horizons:wheelOfFortune";
}
void refreshWheelOfFortune(CGHeroInstance & hero)
{
	if(!hasWheelOfFortune(&hero))
		return;
	hero.removeBonuses(CSelector([](const Bonus * bonus)
	{
		return bonus && bonus->stacking.starts_with(prefix);
	}));
	for(const auto & [spellKey, creatureKey, upgrades] : {
		std::tuple{"new-horizons:bestowLuck", "core:genie", true},
		std::tuple{"new-horizons:turnOfFate", "core:masterGenie", false}})
	{
		const auto spell = SpellID(SpellID::decode(spellKey));
		const auto creature = CreatureID(CreatureID::decode(creatureKey));
		if(!spell.hasValue() || !creature.hasValue())
			throw std::runtime_error("Wheel of Fortune content is not registered");
		auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELLCASTER,
			BonusSource::HERO_SPECIAL, 0, BonusSourceID(hero.getHeroTypeID()), BonusSubtypeID(spell));
		bonus->stacking = std::string(prefix) + spellKey;
		bonus->limiter = std::make_shared<CCreatureTypeLimiter>(*creature.toCreature(), upgrades);
		hero.addNewBonus(bonus);
	}
}
SpellID genieLuckSpell(bool hostile)
{
	// Optional module content must not produce unresolved-ID diagnostics in
	// ordinary or historical games that have no Genie/Sage policy installed.
	const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "spell",
		hostile ? "new-horizons:turnOfFate" : "new-horizons:bestowLuck", true);
	return SpellID(id.value_or(-1));
}
bool isGenieLuckSpell(SpellID spell)
{
	return spell.hasValue() && (spell == genieLuckSpell(false) || spell == genieLuckSpell(true));
}
bool isExplicitGenieLuckCast(const battle::Unit & caster, SpellID spell)
{
	if(!isGenieLuckSpell(spell))
		return false;
	const auto bonus = caster.getBonus(Selector::typeSubtype(BonusType::SPELLCASTER, BonusSubtypeID(spell)));
	return bonus && !bonus->parameters && bonus->source == BonusSource::HERO_SPECIAL
		&& bonus->sid == BonusSourceID(HeroTypeID(HeroTypeID::decode("core:iona")))
		&& bonus->stacking.starts_with(prefix);
}
SpellID genieLuckStatusSpell(const battle::Unit & unit, SpellID family)
{
	if(family != SpellID::FORTUNE && family != SpellID::MISFORTUNE)
		return SpellID::NONE;
	const auto bonuses = unit.getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(family)));
	for(const auto & bonus : *bonuses)
		if(bonus && bonus->stacking == effectMarker && bonus->type == BonusType::LUCK)
			return genieLuckSpell(family == SpellID::MISFORTUNE);
	return SpellID::NONE;
}
bool countersGenieLuck(const Bonus & bonus, SpellID ordinaryFamily)
{
	return bonus.source == BonusSource::SPELL_EFFECT && bonus.stacking == effectMarker
		&& (ordinaryFamily == SpellID::FORTUNE || ordinaryFamily == SpellID::MISFORTUNE);
}
}
