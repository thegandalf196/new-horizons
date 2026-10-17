/*
 * ElementalRebirthLineage.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include "../constants/EntityIdentifiers.h"
#include <array>
namespace battle { class Unit; }
namespace newHorizonsElementalRebirth
{
/// Existing lineage predicates, shared by capture and consumption validation.
const std::array<CreatureID, 5> & canonicalElementals();
bool isCanonicalElemental(CreatureID creature);
bool isFirstGenerationOutput(const battle::Unit & unit);
}
