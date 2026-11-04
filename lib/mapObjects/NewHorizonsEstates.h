/*
 * Part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "../ResourceSet.h"

class CGHeroInstance;
class CGTownInstance;
class CArmedInstance;
class PlayerState;

namespace newHorizonsEstates
{
DLL_LINKAGE bool residentHasPerk(const CGTownInstance & town, const CGHeroInstance * hero, const char * perk);
DLL_LINKAGE bool townHasPerk(const CGTownInstance & town, const char * perk);
/// Explicit actual recruitment recipient. A bare town has no purchasing hero.
DLL_LINKAGE const CGHeroInstance * recruitmentBuyer(const CArmedInstance * recipient);
/// Paid components round upward AFTER multiplying by the complete order quantity.
DLL_LINKAGE ResourceSet recruitmentCost(const ResourceSet & unitCost, int32_t amount, bool conscription);
DLL_LINKAGE int32_t affordableRecruitment(const ResourceSet & unitCost, const ResourceSet & resources,
	int32_t available, bool conscription);
DLL_LINKAGE ResourceSet buildingCost(const ResourceSet & baseCost, bool architect);
DLL_LINKAGE int32_t bankingWeeklyGold(const PlayerState & state);
DLL_LINKAGE int32_t mercenaryWeeklyCount(const PlayerState & state);
}
