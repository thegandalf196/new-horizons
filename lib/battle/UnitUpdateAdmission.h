/*
 * UnitUpdateAdmission.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include <cstdint>
namespace battle
{
class Unit;
/// Internal preparation decision, NOT a wire operation or completed UPDATE.
enum class UnitUpdateGate { APPLY, IGNORE_HEALTH_STOP, IGNORE_STATE_STOP, CHECK_RESURRECTION_SPACE };
/// Exact legacy query order/count. No JSON access, placement build or mutation.
DLL_LINKAGE UnitUpdateGate classifyUnitUpdateGate(const Unit & unit, int64_t healthDelta);
}
