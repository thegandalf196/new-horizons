/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../bonuses/OwnedRecipientBonusGraph.h"

namespace battle::ownedFrameCapture
{
/// INTERNAL synchronous source read. Caller retains the actual receive/state/root
/// interval and supplies its exact graph-local node order. This captures raw
/// exports and canonical route ordinals ONLY; it never evaluates a native
/// predicate/updater or attaches a node. Node addresses are consumed immediately.
/// FULL variants use detach then attach; NATIVE variants deliberately use neither.
DLL_LINKAGE OwnedRecipientBonusGraph::SourceRelation captureFormRelation(
	const CBonusSystemNode & recipient, const CBonusSystemNode & creature, bool attach,
	OwnedBonusEvaluationFacts sourceFacts, const std::vector<const CBonusSystemNode *> & nodes,
	OwnedRecipientBonusGraph::CaptureInput & input);
}
