/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsFrostMantle.h"
#include "../entities/hero/NewHorizonsCastleSpecialties.h"
#include "../mapObjects/CGHeroInstance.h"

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
	result.description.appendRawString("Frost Mantle chill");
	return result;
}
}
