/*
 * IncomingSpellDamage.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "CSpell.h"
#include "IncomingMagicalDamage.h"
#include "MagicalDamageReduction.h"
#include "../battle/BattleAttackInfo.h"
#include "../battle/IBattleInfoCallback.h"
#include "../battle/Unit.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/Bonus.h"
#include <limits>

namespace
{
int64_t totalIncomingPercent(const BonusList & bonuses)
{
	int64_t totalPercent = 0;
	constexpr int64_t maximum = std::numeric_limits<int64_t>::max();
	constexpr int64_t minimum = std::numeric_limits<int64_t>::min();
	for(const auto & bonus : bonuses)
	{
		const int64_t value = bonus->val;
		if(value > 0 && totalPercent > maximum - value)
			totalPercent = maximum;
		else if(value < 0 && totalPercent < minimum - value)
			totalPercent = minimum;
		else
			totalPercent += value;
	}

	return totalPercent;
}

int64_t scaleIncomingDamage(int64_t damage, int64_t totalPercent)
{
	constexpr int64_t maximum = std::numeric_limits<int64_t>::max();
	if(totalPercent <= -100)
		return 0;
	const int64_t multiplier = totalPercent > maximum - 100 ? maximum : totalPercent + 100;
	const int64_t wholeDamage = damage / 100;
	const int64_t fractionalDamage = damage % 100;
	const int64_t wholeMultiplier = multiplier / 100;
	const int64_t fractionalMultiplier = multiplier % 100;
	const int64_t scaledFraction = fractionalDamage * wholeMultiplier
		+ fractionalDamage * fractionalMultiplier / 100;
	if(wholeDamage > maximum / multiplier)
		return maximum;
	const int64_t scaledWholeDamage = wholeDamage * multiplier;
	if(scaledWholeDamage > maximum - scaledFraction)
		return maximum;
	return scaledWholeDamage + scaledFraction;
}
}

int64_t CSpell::applyIncomingElementalDamageBonus(const battle::Unit * affectedCreature, const int64_t damage) const
{
	if(!affectedCreature || !isMagical() || !isDamage()
		|| damageElement == SpellDamageElement::NONE || damage <= 0)
		return damage;

	const auto bonuses = affectedCreature->getBonusesOfType(BonusType::ELEMENTAL_SPELL_DAMAGE_RECEIVED,
		BonusSubtypeID(BonusCustomSubtype(static_cast<int32_t>(damageElement))));
	if(bonuses->empty())
		return damage;

	return scaleIncomingDamage(damage, totalIncomingPercent(*bonuses));
}

int64_t CSpell::applyIncomingMagicalDamageBonus(const battle::Unit * unit, int64_t damage) const
{
	// An omitted subtype is the all-magical modifier. Exact spell subtypes retain
	// their historical earlier ordering; physical abilities never qualify.
	if(!unit || !isMagical() || damage <= 0) return damage;
	return spells::applyIncomingMagicalDamageModifier(*unit, damage);
}

int64_t spells::incomingMagicalDamagePercent(const battle::Unit & target)
{
	const auto bonuses = target.getBonusesOfType(BonusType::MORE_DAMAGE_FROM_SPELL, BonusSubtypeID());
	return std::max<int64_t>(0, totalIncomingPercent(*bonuses));
}

int64_t spells::applyIncomingMagicalDamageModifier(const battle::Unit & target, int64_t damage)
{
	if(damage <= 0) return damage;
	const auto percent = incomingMagicalDamagePercent(target);
	return percent == 0 ? damage : scaleIncomingDamage(damage, percent);
}

int64_t spells::adjustUnmitigatedMagicalDamage(const battle::Unit & target, int64_t damage)
{
	const int reduction = target.intrinsicMagicalDamageReductionPercent();
	const auto mitigated = reduction > 0 ? calculateMagicalDamageReduction(damage,
		std::vector<int>{reduction}, targetMdrPenetrations(target)).damageWithPenetration : damage;
	return applyIncomingMagicalDamageModifier(target, mitigated);
}

static void recalculateMagicalDamageCasualties(const battle::Unit & target, DamageEstimation & damage)
{
	const auto killedBy = [&](int64_t amount) -> int64_t
	{
		const auto integrity = target.getPhantomIntegrity();
		if(integrity > 0)
			return amount >= integrity ? target.getCount() : 0;
		const auto firstHealth = target.getFirstHPleft();
		return amount < firstHealth ? 0 : std::min<int64_t>(target.getCount(),
			1 + (amount - firstHealth) / std::max<uint32_t>(1, target.getMaxHealth()));
	};
	damage.kills.min = killedBy(damage.damage.min);
	damage.kills.max = killedBy(damage.damage.max);
}

void spells::applyIncomingMagicalCreatureDamage(const BattleAttackInfo & attack, DamageEstimation & damage)
{
	if(attack.physicalDamage || !attack.defender) return;
	const auto percent = incomingMagicalDamagePercent(*attack.defender);
	if(percent == 0) return;
	// The existing Lua stage already capped this shot. For a nonnegative
	// vulnerability, reapplying the same cap is equivalent to scaling before it.
	const int capPercent = attack.defender->valOfBonuses(BonusType::DAMAGE_RECEIVED_CAP);
	const int64_t cap = capPercent > 0 ? static_cast<int64_t>(attack.defender->getMaxHealth())
		* capPercent / 100 : std::numeric_limits<int64_t>::max();
	damage.damage.min = std::min(cap, scaleIncomingDamage(damage.damage.min, percent));
	damage.damage.max = std::min(cap, scaleIncomingDamage(damage.damage.max, percent));
	recalculateMagicalDamageCasualties(*attack.defender, damage);
}
