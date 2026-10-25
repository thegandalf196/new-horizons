/* TownGarrisonAdmission.h, part of VCMI engine; GPL v2.0 or later. */
#pragma once

#include "../Goals/Composition.h"
#include <optional>

class CGTownInstance;
class CGHeroInstance;
class CCreatureSet;
class CPlayerSpecificInfoCallback;

namespace NK2AI
{
class IArmyManager;
struct HitMapInfo;
namespace Goals { class ExecuteHeroChain; }

DLL_EXPORT bool canPrepareArrivingTownGarrison(const CGTownInstance & town, const CGHeroInstance & hero,
	const CCreatureSet & arrivalArmy, const IArmyManager & armies, const CPlayerSpecificInfoCallback & callback);
DLL_EXPORT void appendArrivingTownDefence(Goals::TGoalVec & sequence, const CGTownInstance & town,
	const Goals::ExecuteHeroChain & arrival, bool lockDefender, const IArmyManager & armies,
	const CPlayerSpecificInfoCallback & callback);
DLL_EXPORT bool canPrepareCurrentTownGarrison(const CGTownInstance & town, const IArmyManager & armies,
	const CPlayerSpecificInfoCallback & callback);
DLL_EXPORT std::optional<Goals::Composition> currentVisitorGarrisonDefence(const CGTownInstance & town,
	const HitMapInfo & threat, const IArmyManager & armies, const CPlayerSpecificInfoCallback & callback);
}
