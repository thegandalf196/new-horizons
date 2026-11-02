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
		std::map<CreatureID, size_t> traitGraphs; // Pure creature roots in the same F1 batch, evaluated ONLY after cloning.
		const CUnitState * values = nullptr; // Native caller keeps its actual short interval through this call.
	};
	/// Ordered form-source recipes share one clone pool with accepted arrivals.
	/// Queries and source-export handles are derived AFTER canonical replay.
	static std::vector<std::shared_ptr<OwnedUnitBonusFrame>> fromOwnedSourceRecipes(
		std::vector<OwnedRecipientBonusGraph::CaptureInput> graphs, std::vector<Recipe> recipes,
		std::vector<std::vector<OwnedRecipientBonusGraph::SourceRelation>> relations,
		const OwnedBonusCaptureCodec & codec);
	static std::vector<std::shared_ptr<OwnedUnitBonusFrame>> fromOwnedInputs(
		std::vector<OwnedRecipientBonusGraph::CaptureInput> graphs, std::vector<Recipe> recipes,
		const OwnedBonusCaptureCodec & codec);
private:
	static std::vector<std::shared_ptr<OwnedUnitBonusFrame>> build(
		std::vector<OwnedRecipientBonusGraph::CaptureInput> graphs, std::vector<Recipe> recipes,
		std::optional<std::vector<std::vector<OwnedRecipientBonusGraph::SourceRelation>>> relations,
		const OwnedBonusCaptureCodec & codec);
};
}
