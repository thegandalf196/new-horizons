/*
 * Part of VCMI / New Horizons.
 * Authors: listed in file AUTHORS in main folder.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsNecromancy.h"

namespace newHorizonsNecromancy
{
void NecromancyResult::validateReclaimerSerialization(bool supported) const
{
	if(!isReclaimerSummaryValid())
		throw std::runtime_error("Invalid Necromancy Dark Reclaimer summary");
	if(reclaimerZombiesRaised != 0 && !supported)
		throw std::runtime_error("Cannot write Dark Reclaimer summary to an older format");
}
}
