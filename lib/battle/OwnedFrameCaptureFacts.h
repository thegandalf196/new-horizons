/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../bonuses/OwnedRecipientBonusGraph.h"
#include "OwnedUnitCreatureResolver.h"
#include <set>
class CCreature;
class CStack;
class CBattleInfoCallback;
// Internal engine capture helper; not exported as a native policy API.
namespace battle { BonusAcceptanceFacts captureOwnedBonusAcceptance(const CStack & source); }
namespace battle::ownedFrameCapture
{
struct Requirements
{
	std::set<CreatureID> families, sourceCreatures, adjacentFamilies;
	bool terrain = false, adjacency = false;
	std::vector<std::shared_ptr<const Bonus>> secondaryRecords;
};
DLL_LINKAGE Requirements requirements(const std::vector<BonusList> & raw);
DLL_LINKAGE OwnedBonusEvaluationFacts facts(const CBonusSystemNode & node, const Requirements & needed,
	const std::vector<const CCreature *> & visibleForms, const CStack * recipient = nullptr,
	const CCreature * effective = nullptr);
DLL_LINKAGE OwnedUnitCreatureResolver::Creature descriptor(const CCreature & type);
}
