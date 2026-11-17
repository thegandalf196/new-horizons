/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsFocusMagic.h"
#include "FocusMagicValueState.h"
#include "Unit.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusParameters.h"

namespace newHorizonsFocusMagic
{
bool isEnchantmentRecord(const Bonus & bonus)
{
	return bonus.source == BonusSource::SPELL_EFFECT && bonus.sid.toString() == SPELL;
}

std::vector<Bonus> nativeRecords(const battle::Unit & unit)
{
	std::vector<Bonus> result;
	const auto bonuses = unit.getBonusesFrom(BonusSource::SPELL_EFFECT);
	for(const auto & bonus : *bonuses)
		if(isEnchantmentRecord(*bonus)) result.push_back(*bonus);
	return result;
}

bool isChargeMarker(const Bonus & bonus)
{
	return isEnchantmentRecord(bonus) && bonus.type == BonusType::NONE && bonus.duration == BonusDuration::ONE_BATTLE
		&& bonus.stacking == SPELL && bonus.val >= 1 && bonus.val <= MAX_CHARGES;
}

int charges(const battle::Unit & unit)
{
	int result = 0;
	const auto bonuses = unit.getBonusesOfType(BonusType::NONE);
	for(const auto & bonus : *bonuses)
		if(isChargeMarker(*bonus)) result = std::max(result, bonus->val);
	return result;
}

bool active(const battle::Unit & unit) { return charges(unit) > 0; }

bool converted(const battle::Unit & unit)
{
	if(!active(unit)) return false;
	const auto bonuses = unit.getBonusesOfType(BonusType::SHOOTER);
	for(const auto & bonus : *bonuses)
		if(isEnchantmentRecord(*bonus)) return true;
	return false;
}

std::vector<Bonus> enchantment(const battle::Unit & unit, SpellID spell, int32_t additionalRangedPercent)
{
	if(additionalRangedPercent < 0 || additionalRangedPercent > 100)
		throw std::invalid_argument("Invalid Focus Magic ranged enhancement");
	// Recasts must classify the original shooter after excluding this enchantment.
	bool nativeShooter = false;
	const auto bonuses = unit.getBonusesOfType(BonusType::SHOOTER);
	for(const auto & bonus : *bonuses)
		if(!isEnchantmentRecord(*bonus)) nativeShooter = true;
	return enchantment(nativeShooter, spell, additionalRangedPercent);
}

Consumption consume(const battle::Unit & unit)
{
	const int remaining = charges(unit);
	if(remaining <= 0) return {};
	return consumeRecords(nativeRecords(unit), remaining);
}
}
