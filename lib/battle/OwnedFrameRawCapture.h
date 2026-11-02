/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "OwnedFrameCaptureFacts.h"
#include "../bonuses/BonusGraphQuery.h"
namespace battle::ownedFrameCapture
{
struct RawGraph
{
	OwnedRecipientBonusGraph::CaptureInput input;
	std::vector<const CBonusSystemNode *> nodes; // Immediate lease-only ordinal map; NEVER in the owned result.
	std::vector<size_t> sourceLocalSpells;
};
RawGraph rawGraph(const BonusGraphQuery &, const CBonusSystemNode & root);
void finishFacts(RawGraph &, const CStack * recipient, const CCreature * effective,
	const std::vector<const CCreature *> & visibleForms);
}
