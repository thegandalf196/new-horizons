/*
* ArmyFormation.h, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/
#pragma once

#include "../AIUtility.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"

namespace NK2AI
{

/// Read-only preflight for army-arrangement requests.  New Horizons applies a
/// per-stack Leadership limit on heroes, so an AI transfer must be checked for
/// the stack that will exist after the request.  The server remains the final
/// authority; these helpers only prevent Nullkiller from knowingly submitting
/// requests that the server must reject.
namespace armyFormation
{
bool canReceiveStack(const CArmedInstance * destination, CreatureID creature, int resultingCount);
bool canSwapStacks(const CArmedInstance * first, const CArmedInstance * second,
	SlotID firstSlot, SlotID secondSlot);
bool canMergeOrSwapStacks(const CArmedInstance * source, const CArmedInstance * destination,
	SlotID sourceSlot, SlotID destinationSlot);
/// Mirrors the server's whole-army merge plan used when a visiting hero is
/// moved into an empty town garrison. This is a read-only AI preflight; the
/// authoritative move remains on the server.
bool canMergeArmies(const CArmedInstance * source, const CArmedInstance * destination);
/// Returns whether a garrison-hero swap is admissible, including the
/// town-army merge performed when the town has no garrison hero.
bool canSwapGarrisonHero(const CGTownInstance * town);
bool canSplitStack(const CArmedInstance * source, const CArmedInstance * destination,
	SlotID sourceSlot, SlotID destinationSlot, int resultingDestinationCount);
int maxLegalTransferCount(const CArmedInstance * source, const CArmedInstance * destination,
	SlotID sourceSlot, SlotID destinationSlot);
}

struct HeroPtr;
class AIGateway;
class FuzzyHelper;
class Nullkiller;

class DLL_EXPORT ArmyFormation
{
private:
	std::shared_ptr<CCallback> cb; //this is enough, but we downcast from CCallback

public:
	ArmyFormation(std::shared_ptr<CCallback> CB, const Nullkiller * aiNk): cb(CB) {}

	void rearrangeArmyForSiege(const CGTownInstance * town, const CGHeroInstance * attacker);

	void rearrangeArmyForWhirlpool(const CGHeroInstance * hero);

	void addSingleCreatureStacks(const CGHeroInstance * hero);
};

}
