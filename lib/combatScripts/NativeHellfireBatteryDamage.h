/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include <cstdint>

class CBattleInfoCallback;
struct DamageAttackInfo;
struct DamageEstimation;
namespace nativeDamage
{
struct Facts;
namespace hellfireBattery
{
struct Budget
{
	int64_t physical = 0;
	int64_t fire = 0;
};
DLL_LINKAGE Budget splitBudget(double commonDamage, int firePermille);
DLL_LINKAGE int firePermille(const CBattleInfoCallback & battle, const DamageAttackInfo & info);
DLL_LINKAGE DamageEstimation calculate(const CBattleInfoCallback & battle, const DamageAttackInfo & info,
	const Facts & facts, double physicalFactor, double beforeDefenseFactor, int firePermille, double commonFactor);
}
}
