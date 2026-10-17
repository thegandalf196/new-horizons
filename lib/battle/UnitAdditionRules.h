/*
 * UnitAdditionRules.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include <cstddef>
#include "Unit.h"
#include "HeroCommand.h"
namespace battle
{
DLL_LINKAGE uint32_t nextUnitAdditionId(size_t size, const JsonNode & rules);
DLL_LINKAGE UnitInfo decodeUnitAddition(uint32_t id, const JsonNode & data, const JsonNode & rules, size_t size);
DLL_LINKAGE void validateUnitAdditionCapacity(const UnitInfo & info, int32_t effectiveMaxHP);
DLL_LINKAGE void validateRebirthAdditionSide(const UnitInfo & info);
DLL_LINKAGE bool unitAdditionNeedsVengeance(const Unit & unit, const HeroOrderState * orderState,
	int32_t round, const CGHeroInstance * hero);
}
