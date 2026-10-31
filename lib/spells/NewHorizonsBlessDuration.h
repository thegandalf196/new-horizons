/* NewHorizonsBlessDuration.h, part of VCMI / New Horizons; GPL v2.0 or later. */
#pragma once
#include "../../Global.h"
#include <cstdint>
class CGHeroInstance;
namespace newHorizonsMagic
{
DLL_LINKAGE int32_t blessOrdinaryDuration(int32_t power, int32_t coefficient, int32_t warcasting,
	int32_t empower, int32_t specialty);
/// Adds existing caster duration bonuses before general Extend/Echo adjustment.
DLL_LINKAGE int32_t blessCasterDuration(const CGHeroInstance & hero, int32_t ordinary, bool brimstone);
}
