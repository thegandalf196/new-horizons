/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "OwnedUnitBonusFrame.h"
namespace battle
{
/// Pure owned-value construction, not native callback/lifetime/visibility admission.
class DLL_LINKAGE OwnedUnitBonusFrameBatch final
{
public:
	struct Peer
	{
		OwnedUnitBonusFrame::Identity identity;
		size_t graph;
		OwnedRecipientBonusGraph::RootQuery query;
		const CUnitState * values = nullptr; // Consumed synchronously, NEVER stored.
		std::map<CreatureID, std::pair<size_t, OwnedRecipientBonusGraph::RootQuery>> variants;
	};
	struct Recipe
	{
		OwnedUnitBonusFrame::Inputs inputs; // Raw nodes/extras/codec and pre-owned peers must be empty.
		size_t baseGraph;
		std::map<CreatureID, std::pair<size_t, size_t>> formGraphs; // FULL/NATIVE are intentionally independent.
		std::vector<Peer> peers; // Exact original callback order excluding recipient slot.
		const CUnitState * values = nullptr; // Native caller keeps its actual short interval through this call.
	};
	static std::vector<std::shared_ptr<OwnedUnitBonusFrame>> fromOwnedInputs(
		std::vector<OwnedRecipientBonusGraph::CaptureInput> graphs, std::vector<Recipe> recipes,
		const OwnedBonusCaptureCodec & codec);
};
}
