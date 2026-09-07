/*
 * NewHorizonsMasteryEffects.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once
#include "NewHorizonsMasteryRules.h"
#include <memory>
#include <vector>

struct Bonus;
class CGHeroInstance;

namespace newHorizonsHeroes
{
/// Bonuses to attach to the owning hero after authoritative selection. Volley is
/// read by the server using its Ballista subtype; other effects inherit only to
/// Ballista creatures. Construction alone neither grants nor records a mastery.
DLL_LINKAGE std::vector<std::shared_ptr<Bonus>> masteryBonuses(const MasteryOption & choice);
/// Contextual, provisional valuation shared by adventure controllers. Uses only
/// the owning hero's visible army/equipment, not hidden enemy state or option order.
DLL_LINKAGE int chooseMasteryForArmy(const MasteryOffer & offer, const CGHeroInstance & hero);
}
