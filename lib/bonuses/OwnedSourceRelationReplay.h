/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "OwnedRecipientBonusGraph.h"

/// INTERNAL F1 construction stages. Input order was captured under the caller's
/// source interval. Neither stage discovers nodes, grants visibility or retains
/// native identities. Replay accepts only the preceding batch's owned clones.
void validateOwnedSourceRelations(const OwnedRecipientBonusGraph::CaptureInput & input,
	const std::vector<std::shared_ptr<const Bonus>> & raw,
	const std::vector<OwnedRecipientBonusGraph::SourceRelation> & relations);
std::vector<std::shared_ptr<Bonus>> replayOwnedSourceRelations(
	OwnedRecipientBonusGraph::CaptureInput & input, std::vector<std::shared_ptr<Bonus>> records,
	const std::vector<OwnedRecipientBonusGraph::SourceRelation> & relations);
