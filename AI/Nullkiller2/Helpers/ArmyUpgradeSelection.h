/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../../../lib/constants/EntityIdentifiers.h"
#include "../../../lib/ResourceSet.h"
#include <optional>
class CGHeroInstance;
class CStackInstance;
class UpgradeInfo;
namespace NK2AI::armyUpgrade
{
/// Chooses from actual offers only; retains ordinary AIValue ordering and affordability.
/// The recognized free Wraith-to-Ghost added ability can break an otherwise inert tie.
std::optional<CreatureID> choose(const CStackInstance & stack, const UpgradeInfo & offers,
	const CGHeroInstance * carrier, const TResources & available);
}
