/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include <array>
#include <utility>

struct DamageAttackInfo;
namespace nativeDamage
{
struct Facts;
std::pair<double, double> baseDamage(const DamageAttackInfo &, const Facts &);
std::array<double, 24> factors(const DamageAttackInfo &, const Facts &);
struct DamageModifierTotals
{
	double raising = 1.0;
	double lowering = 1.0;
	double legacyProtection = 1.0;
	double commonRaising = 1.0;
	double commonLowering = 1.0;
};
struct FinalDamageFactors
{
	double heroOrder = 1.0, preemptive = 1.0, cleave = 1.0, archery = 1.0;
	double followUp = 1.0, activation = 1.0, sylvan = 1.0, sylvanDefense = 1.0;
	double specialty = 1.0, luck = 1.0, phantom = 1.0;
	double immovable = 1.0, bastion = 1.0, counterBattery = 1.0, inspiration = 1.0, shatter = 1.0;
};
DLL_LINKAGE DamageModifierTotals foldDamageModifiers(const DamageAttackInfo &, const Facts &, bool includeCommon = false);
DLL_LINKAGE FinalDamageFactors finalDamageFactors(const DamageAttackInfo &, const Facts &);
DLL_LINKAGE double finalDamageFactor(const FinalDamageFactors &, double baseFactor, bool physicalDefenses);
DLL_LINKAGE double commonDamageFactor(const FinalDamageFactors &, const DamageModifierTotals &);
}
