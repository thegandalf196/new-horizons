/* TownGarrisonAdmission.h, part of VCMI engine; GPL v2.0 or later. */
#pragma once

#include "../Goals/Composition.h"
#include <optional>

class CGTownInstance;
class CPlayerSpecificInfoCallback;

namespace NK2AI
{
class IArmyManager;
struct HitMapInfo;

DLL_EXPORT bool canPrepareCurrentTownGarrison(const CGTownInstance & town, const IArmyManager & armies,
	const CPlayerSpecificInfoCallback & callback);
DLL_EXPORT std::optional<Goals::Composition> currentVisitorGarrisonDefence(const CGTownInstance & town,
	const HitMapInfo & threat, const IArmyManager & armies, const CPlayerSpecificInfoCallback & callback);
}
