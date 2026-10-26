/*
 * Part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../lib/constants/EntityIdentifiers.h"
#include <cstdint>
#include <string>
#include <vector>
class MetaString;
namespace newHorizonsNecromancy { struct NecromancyResult; }
namespace necromancyPresentation
{
struct RecoveryOutput
{
	CreatureID creature;
	int32_t count;
};
/// Read actual restored troops; never derive them from fresh raising or casualties.
std::vector<RecoveryOutput> recoveryOutputs(const newHorizonsNecromancy::NecromancyResult & result);
void appendRecovery(MetaString & text, const newHorizonsNecromancy::NecromancyResult & result);
void appendFreshDelivery(MetaString & text, const newHorizonsNecromancy::NecromancyResult & result, const std::string & ossuaryTownName);
void appendConversions(MetaString & text, const newHorizonsNecromancy::NecromancyResult & result);
}
