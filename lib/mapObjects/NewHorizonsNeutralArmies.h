/*
 * NewHorizonsNeutralArmies.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "../GameConstants.h"

class CArmedInstance;
class CMap;
class IGameSettings;
class JsonNode;
namespace Rewardable { struct Configuration; }

namespace newHorizonsNeutralArmies
{
inline constexpr auto SETTING_KEY = "neutralArmySizePercent";

/// Trial percentage: 100 is identity, 50 is the opt-in half-size experiment.
DLL_LINKAGE int percentage(const IGameSettings & settings);
DLL_LINKAGE TQuantity scaleCount(TQuantity count, int percent);
DLL_LINKAGE bool eligible(const CArmedInstance & army);
DLL_LINKAGE TQuantity generatedCount(const CArmedInstance & army, TQuantity rawCount);
DLL_LINKAGE void scaleGeneratedGuards(const CArmedInstance & army, Rewardable::Configuration & raw);

/// Fresh-world only: capture effective settings and scale each initial army once.
DLL_LINKAGE void initializeWorld(CMap & map);

/// Settings admission also protects legacy read/write boundaries.
DLL_LINKAGE void validateOverrides(const JsonNode & settings, bool supported);
DLL_LINKAGE void validateSerialization(const IGameSettings & settings, bool supported);
}
