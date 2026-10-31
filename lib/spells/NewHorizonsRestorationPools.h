/* NewHorizonsRestorationPools.h, part of VCMI / New Horizons; GPL v2.0 or later. */
#pragma once
#include "../../Global.h"
#include <cstdint>
namespace newHorizonsMagic
{
DLL_LINKAGE int64_t resurrectionRestorationPool(int32_t power, int32_t coefficient, int32_t warcasting,
	int32_t empower, int32_t specialty, int32_t recipientPowerBonus = 0);
DLL_LINKAGE int64_t guardianSpiritHitPoints(int32_t power, int32_t coefficient, int32_t warcasting,
	int32_t empower, int32_t specialty, bool healer, bool guardian, int32_t recipientPowerBonus = 0);
}
