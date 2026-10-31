/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "BonusGraphQuery.h"
#include "OwnedBonusEvaluationFacts.h"
#include "OwnedBonusPayloads.h"
#include <unordered_map>
/// Pure owned inheritance storage, not live capture/admission. Context currently
/// covers static Hero/Army/count facts and context-independent limiters only.
class DLL_LINKAGE OwnedRecipientBonusGraph final : public BonusGraphQuery, boost::noncopyable
{
public:
	struct NodeInput
	{
		OwnedBonusEvaluationFacts facts;
		bool primaryRating = false;
		std::vector<std::shared_ptr<const Bonus>> local;
		std::vector<size_t> parents;
	};
	static std::unique_ptr<OwnedRecipientBonusGraph> fromOwnedInputs(std::vector<NodeInput> inputs, size_t root);
	~OwnedRecipientBonusGraph();
	const CBonusSystemNode & root() const;
	/// Returned raw/effective payload borrows must retire before this owner.
	BonusList collect() const;
	BonusList query(const CSelector & selector, bool unstacked = false) const;
	const BonusList & local(const CBonusSystemNode & node) const override;
	const TCNodesVector & parents(const CBonusSystemNode & node) const override;
	const TNodesVector & propagatingParents(const CBonusSystemNode & node) const override;
	const TNodesVector & children(const CBonusSystemNode & node) const override;
	BonusEvaluationContext evaluation(const CBonusSystemNode & node) const override;
private:
	struct Node;
	std::unique_ptr<ownedBonusPayloads::Captured> payloads; // Retires after all node lists.
	std::vector<std::unique_ptr<Node>> nodes;
	std::unordered_map<const CBonusSystemNode *, size_t> indices;
	TNodesVector emptyEdges;
	size_t rootIndex = 0;
	OwnedRecipientBonusGraph() = default;
	const Node & entry(const CBonusSystemNode & node) const;
};
