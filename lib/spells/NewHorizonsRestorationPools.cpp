/*
 * NewHorizonsRestorationPools.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsRestorationPools.h"
#include "NewHorizonsMagic.h"
#include "ISpellMechanics.h"
namespace newHorizonsMagic
{
namespace
{
int64_t recipientPowerTerm(int64_t numerator, int32_t coefficient, int32_t warcasting,
	int32_t empower, int32_t specialty, int32_t recipientPowerBonus)
{
	if(recipientPowerBonus < 0 || recipientPowerBonus > 1000)
		throw std::invalid_argument("Invalid restoration recipient multiplier");
	return spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		numerator * (100 + recipientPowerBonus), 100, coefficient, warcasting, empower, specialty);
}
}
int64_t resurrectionRestorationPool(int32_t power, int32_t coefficient, int32_t warcasting,
	int32_t empower, int32_t specialty, int32_t recipientPowerBonus)
{
	return RESURRECTION_BASE_POOL_HP + recipientPowerTerm(
		static_cast<int64_t>(RESURRECTION_SPELL_POWER_HP_PER_POINT) * std::max(0, power),
		coefficient, warcasting, empower, specialty, recipientPowerBonus);
}
int64_t guardianSpiritHitPoints(int32_t power, int32_t coefficient, int32_t warcasting,
	int32_t empower, int32_t specialty, bool healer, bool guardian, int32_t recipientPowerBonus)
{
	auto term = recipientPowerTerm(2LL * power, coefficient, warcasting, empower, specialty, recipientPowerBonus);
	if(healer)
		term = term * 120 / 100;
	auto pool = 50 + term;
	if(guardian)
		pool = pool * 125 / 100;
	return pool;
}
}
