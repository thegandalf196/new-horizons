/* NewHorizonsPlacementPower.h, part of VCMI / New Horizons; GPL v2.0 or later. */
#pragma once
#include "../../Global.h"
#include <cstdint>
class JsonNode;
namespace newHorizonsMagic
{
DLL_LINKAGE int64_t verdantPrisonHitPoints(int32_t power, int32_t coefficient, int32_t warcasting, int32_t empower, bool warden);
DLL_LINKAGE int64_t transfigureHitPoints(int32_t power, int32_t coefficient, int32_t warcasting, int32_t empower,
	int32_t footprint, bool matterShaper);
DLL_LINKAGE int32_t earthquakeSectionCount(const JsonNode & profile, int32_t power, int32_t coefficient, int32_t warcasting, int32_t empower);
DLL_LINKAGE int64_t earthquakeRawDamage(const JsonNode & profile, int32_t power, int32_t coefficient, int32_t warcasting, int32_t empower);
DLL_LINKAGE int64_t earthquakeStructuralDamage(int32_t base, bool geomancer);
}
