/* NewHorizonsTimedMagnitudes.h, part of VCMI / New Horizons; GPL v2.0 or later. */
#pragma once
#include "../../Global.h"
#include <cstdint>
namespace newHorizonsMagic
{
DLL_LINKAGE int32_t frailtyDefenseLoss(int32_t power, int32_t coefficient, int32_t warcasting,
	int32_t empower, int32_t specialty, bool withering);
DLL_LINKAGE int64_t hydraCapacityMillionths(int32_t power, int32_t coefficient, int32_t warcasting,
	int32_t empower, int32_t specialty);
DLL_LINKAGE int32_t entangleBaseDuration(int32_t power, int32_t coefficient, int32_t warcasting,
	int32_t empower, bool rootcaller);
DLL_LINKAGE int32_t misfortuneBaseDuration(int32_t power, int32_t coefficient, int32_t warcasting, int32_t empower);
DLL_LINKAGE int32_t misfortuneChanceMultiplier(int32_t power, int32_t coefficient, int32_t warcasting,
	int32_t empower, bool weaver);
DLL_LINKAGE int32_t shieldOfChaosReduction(int32_t power, int32_t coefficient, int32_t warcasting,
	int32_t empower, bool paradox);
DLL_LINKAGE int64_t divineRetributionCap(int32_t power, int32_t coefficient, int32_t warcasting,
	int32_t empower, int32_t crown = 0);
DLL_LINKAGE int64_t divineRetributionPayout(int64_t actualHpDamage, int64_t rawCap, int32_t retributionistPercent);
}
