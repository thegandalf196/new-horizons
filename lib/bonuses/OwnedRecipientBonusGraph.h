/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "BonusGraphQuery.h"
#include "OwnedBonusEvaluationFacts.h"
#include "OwnedBonusPayloads.h"
#include <unordered_map>
#include <unordered_set>
namespace battle { class OwnedUnitBonusFrame; }
/// Pure owned inheritance storage, not live capture/admission. Context currently
/// covers copied scalar/type/terrain/rank/acceptance domains; no live frame/roster.
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
	static std::unique_ptr<OwnedRecipientBonusGraph> fromOwnedInputs(std::vector<NodeInput> inputs, size_t root,
		std::vector<std::shared_ptr<const Bonus>> extraRecords = std::vector<std::shared_ptr<const Bonus>>());
	static std::unique_ptr<OwnedRecipientBonusGraph> fromOwnedInputs(std::vector<NodeInput> inputs, size_t root,
		std::vector<std::shared_ptr<const Bonus>> extraRecords, const OwnedBonusCaptureCodec & codec);
	struct CaptureInput
	{
		std::vector<NodeInput> nodes;
		size_t root = 0;
		std::vector<std::shared_ptr<const Bonus>> extraRecords;
	};
	/// One deep alias pool, separate graph-local entitlements. Pure owned input, not live authority.
	static std::vector<std::unique_ptr<OwnedRecipientBonusGraph>> fromOwnedBatchInputs(std::vector<CaptureInput> inputs);
	static std::vector<std::unique_ptr<OwnedRecipientBonusGraph>> fromOwnedBatchInputs(
		std::vector<CaptureInput> inputs, const OwnedBonusCaptureCodec & codec);
	struct RootQuery
	{
		OwnedBonusEvaluationFacts facts;
		std::vector<size_t> parents; // Node indices, preserving canonical parent visit order/multiplicity.
		std::vector<size_t> records; // Root-local or extra owned payload handles; never native pointers.
	};
	std::vector<size_t> rootLocalRecords() const;
	size_t payloadCount() const;
	/// Read-only borrow; retain owner. Alias identity is shared with raw node records.
	const std::shared_ptr<Bonus> & payload(size_t index) const;
	void validateRootQuery(const RootQuery & query) const;
	BonusList queryRoot(const CSelector & selector, bool unstacked, const RootQuery & query) const;
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
	friend class battle::OwnedUnitBonusFrame;
	BonusList queryRootRecords(const CSelector & selector, bool unstacked, const RootQuery & query, const BonusList & records, const battle::Unit * ownedUnit = nullptr) const;
	struct Node;
	std::shared_ptr<ownedBonusPayloads::Captured> payloads; // Last graph retires the one nested alias pool.
	std::vector<std::shared_ptr<Bonus>> recordValues; // This graph's ordered slice, not other roots' query handles.
	std::vector<std::unique_ptr<Node>> nodes;
	std::unordered_map<const CBonusSystemNode *, size_t> indices;
	TNodesVector emptyEdges;
	std::unordered_set<const Bonus *> inheritedOnlyRecords; // Immutable OWNED clone identities, not live addresses.
	size_t rootIndex = 0;
	size_t rootPayloadStart = 0;
	size_t localPayloadCount = 0;
	OwnedRecipientBonusGraph() = default;
	static std::unique_ptr<OwnedRecipientBonusGraph> build(CaptureInput input,
		std::shared_ptr<ownedBonusPayloads::Captured> payloads, size_t start, size_t localCount, size_t recordCount);
	void validateRecord(size_t index) const;
	const Node & entry(const CBonusSystemNode & node) const;
	const CBonusSystemNode & queryNode(size_t index) const;
};
