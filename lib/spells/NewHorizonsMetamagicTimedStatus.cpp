/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsMetamagicTimedStatus.h"
#include "NewHorizonsTimedUtility.h"
#include "CSpell.h"
#include "../bonuses/Bonus.h"
#include <algorithm>
#include <limits>
#include <cstdlib>
namespace newHorizonsMetamagic
{
namespace
{
bool familyNamed(SpellID accepted, SpellID family, const std::string & key)
{
	const auto * spell = family.toSpell();
	const auto * actual = accepted.toSpell();
	return (spell && spell->getJsonKey() == key) || (actual && actual->getJsonKey() == key);
}
}
bool isChaosShieldMagnitude(const Bonus & bonus, SpellID accepted, SpellID family)
{
	return bonus.valType == BonusValueType::INDEPENDENT_MAX
		&& (bonus.type == BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS
			|| bonus.type == BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS)
		&& familyNamed(accepted, family, "new-horizons:shieldOfChaos");
}
bool isMisfortuneChanceFactor(const Bonus & bonus, SpellID accepted, SpellID family)
{
	return bonus.valType == BonusValueType::INDEPENDENT_MIN
		&& bonus.type == BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS
		&& familyNamed(accepted, family, "core:misfortune");
}
std::optional<int32_t> lightReductionLimit(const Bonus & bonus, SpellID accepted, SpellID family)
{
	if(bonus.type == BonusType::SPELL_DAMAGE_REDUCTION
		&& familyNamed(accepted, family, "new-horizons:holyArmor"))
		return newHorizonsMagic::HOLY_ARMOR_MAXIMUM_REDUCTION;
	if(bonus.type == BonusType::HEAVENLY_GALE
		&& familyNamed(accepted, family, "new-horizons:heavenlyGale"))
		return newHorizonsMagic::HEAVENLY_GALE_MAXIMUM_REDUCTION_BASIS_POINTS;
	return std::nullopt;
}
int32_t chaosShieldMagnitude(int32_t ordinary, const Profile & profile, int32_t marks, int32_t nativeLimit)
{
	return static_cast<int32_t>(std::min<int64_t>(nativeLimit, scaleMagnitude(ordinary, profile, marks)));
}
int32_t misfortuneChanceFactor(int32_t ordinary, const Profile & profile, int32_t marks)
{
	const int64_t reduction = 10000LL - ordinary;
	const int64_t magnitude = scaleMagnitude(std::abs(reduction), profile, marks);
	const int64_t adjusted = 10000LL - (reduction < 0 ? -magnitude : magnitude);
	return static_cast<int32_t>(std::clamp<int64_t>(adjusted, 2500, std::numeric_limits<int32_t>::max()));
}
}
