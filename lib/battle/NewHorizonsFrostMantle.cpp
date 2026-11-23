/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsFrostMantle.h"
#include "../entities/hero/NewHorizonsCastleSpecialties.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../spells/CSpell.h"
#include "../spells/ISpellMechanics.h"
#include "CBattleInfoCallback.h"
#include "Unit.h"

namespace newHorizonsFrostMantle
{
std::optional<Bonus> cloak(const CGHeroInstance * hero)
{
	const auto & rule = newHorizonsHeroes::frostMantleRule(hero);
	const ScriptID script(ScriptID::decode(SCRIPT));
	if(!rule || !script.hasValue())
		return std::nullopt;
	// Hero-sourced, not a Frost Ring spell effect: friendly debuff removal must not strip it.
	Bonus result(BonusDuration::N_TURNS, BonusType::COMBAT_EVENT_TRIGGER, BonusSource::HERO_SPECIAL,
		newHorizonsHeroes::frostMantleReturnedPercent(hero), BonusSourceID(hero->getHeroTypeID()), BonusSubtypeID(script));
	result.turnsRemain = rule->rounds;
	result.stacking = CLOAK_STACKING;
	result.customIconPath = ImagePath::builtin(CLOAK_ICON);
	result.description.appendRawString("Frost Mantle");
	return result;
}

std::optional<Bonus> chill(const CGHeroInstance * hero)
{
	const auto & rule = newHorizonsHeroes::frostMantleRule(hero);
	if(!rule || rule->initiativePenaltyPercent <= 0)
		return std::nullopt;
	Bonus result(BonusDuration::N_TURNS, BonusType::STACKS_INITIATIVE, BonusSource::SPELL_EFFECT,
		-rule->initiativePenaltyPercent, BonusSourceID(SpellID(SpellID::FROST_RING)));
	result.valType = BonusValueType::ADDITIVE_VALUE;
	result.turnsRemain = rule->chillRounds;
	result.stacking = CHILL_STACKING;
	result.customIconPath = ImagePath::builtin(CHILL_ICON);
	result.description.appendRawString("Frost Mantle chill");
	return result;
}

bool chills(const spells::Mechanics & mechanics, const battle::Unit & centre)
{
	// A Frost Ring effect: whatever makes the stack immune to the ring blocks it (accepted 2026-11-23).
	return centre.unitSide() != mechanics.getCasterSide() && chill(mechanics.getHeroCaster()).has_value()
		&& mechanics.isReceptive(&centre);
}

bool chills(const CBattleInfoCallback & battle, const CGHeroInstance & hero, const battle::Unit & centre)
{
	spells::BattleCast cast(&battle, &hero, spells::Mode::HERO, SpellID(SpellID::FROST_RING).toSpell());
	return chills(*cast.getSpell()->battleMechanics(&cast), centre);
}

bool centredOnStack(const spells::Mechanics & mechanics, const battle::Target & target)
{
	if(mechanics.getSpellId() != SpellID::FROST_RING || !mechanics.isGenuineHeroCast() || !mechanics.battle()
		|| target.empty() || !target.front().hexValue.isValid())
		return false;
	// The same centre the authority cloaks or chills after the cast.
	const auto * centre = mechanics.battle()->battleGetUnitByPos(target.front().hexValue, true);
	if(!centre || !centre->alive() || !centre->isValidTarget(false))
		return false;
	const auto * hero = mechanics.getHeroCaster();
	return centre->unitSide() == mechanics.getCasterSide() ? cloak(hero).has_value() : chills(mechanics, *centre);
}
}
