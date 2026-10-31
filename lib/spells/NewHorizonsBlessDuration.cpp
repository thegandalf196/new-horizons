/*
 * NewHorizonsBlessDuration.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsBlessDuration.h"
#include "NewHorizonsMagic.h"
#include "ISpellMechanics.h"
#include "../mapObjects/CGHeroInstance.h"
#include <limits>
namespace newHorizonsMagic
{
int32_t blessOrdinaryDuration(int32_t power, int32_t coefficient, int32_t warcasting,
	int32_t empower, int32_t specialty)
{
	return blessDurationFromPowerTerm(spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		power, BLESS_SPELL_POWER_DURATION_DIVISOR, coefficient, warcasting, empower, specialty));
}
int32_t blessCasterDuration(const CGHeroInstance & hero, int32_t ordinary, bool brimstone)
{
	int64_t duration = ordinary;
	duration += hero.valOfBonuses(BonusType::SPELL_DURATION, BonusSubtypeID());
	duration += hero.valOfBonuses(BonusType::SPELL_DURATION, BonusSubtypeID(SpellID(SpellID::BLESS)));
	duration += spellDurationBonus(&hero, SpellID::BLESS);
	if(brimstone)
		duration += 20;
	if(hasBenedictionPerk(&hero))
		++duration;
	return static_cast<int32_t>(std::clamp<int64_t>(duration, 0, std::numeric_limits<int32_t>::max()));
}
}
