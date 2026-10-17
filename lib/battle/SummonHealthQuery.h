/*
 * SummonHealthQuery.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include <cstdint>
class IBonusBearer;
namespace newHorizonsElementalRebirth
{
/// Original health query/clamp, shared by native and private hypothetical owners.
DLL_LINKAGE int32_t querySummonMaxHP(const IBonusBearer & hypothetical);
}
