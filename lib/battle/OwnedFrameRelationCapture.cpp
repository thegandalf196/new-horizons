/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedFrameRelationCapture.h"
#include "../bonuses/BonusSourceRelationRecipe.h"
#include <stdexcept>
#include <unordered_map>
namespace battle::ownedFrameCapture
{
namespace
{
void destinations(const CBonusSystemNode & node, const std::unordered_map<const CBonusSystemNode *, size_t> & indices,
	std::vector<size_t> & route)
{
	if(bonusSourceOnly(node.getNodeType()))
		throw std::invalid_argument("Owned form destination route must remain parent-only");
	if(const auto found = indices.find(&node); found != indices.end()) route.push_back(found->second);
	TCNodes next;
	bonusRedChildren(liveBonusGraph(), node, next); // Native address order is converted NOW, never reconstructed on shells.
	for(const auto * destination : next) destinations(*destination, indices, route);
}
}
OwnedRecipientBonusGraph::SourceRelation captureFormRelation(
	const CBonusSystemNode & recipient, const CBonusSystemNode & creature, bool attach,
	OwnedBonusEvaluationFacts sourceFacts, const std::vector<const CBonusSystemNode *> & nodes,
	OwnedRecipientBonusGraph::CaptureInput & input)
{
	if(recipient.getNodeType() != BonusNodeType::STACK_BATTLE || creature.getNodeType() != BonusNodeType::CREATURE
		|| sourceFacts.nodeType != BonusNodeType::CREATURE || nodes.size() != input.nodes.size())
		throw std::invalid_argument("Mismatched owned form relation source/route facts");
	std::unordered_map<const CBonusSystemNode *, size_t> indices;
	size_t offset = 0;
	for(size_t i = 0; i < nodes.size(); ++i)
	{
		if(!nodes[i] || !indices.emplace(nodes[i], i).second
			|| nodes[i]->getNodeType() != input.nodes[i].facts.nodeType)
			throw std::invalid_argument("Mismatched owned form relation node ordinal");
		offset += input.nodes[i].local.size();
	}
	offset += input.extraRecords.size() + input.sourceExports.size();
	OwnedRecipientBonusGraph::SourceRelation result{attach, std::move(sourceFacts), {}, {}};
	if(recipient.isHypothetic()) return result; // Exact native relation guard, before source export traversal.
	destinations(recipient, indices, result.destinations);
	forEachPropagatingSourceRecord(liveBonusGraph(), creature,
		[](const CBonusSystemNode & source) -> const BonusList & { return source.getExportedBonusList(); },
		[&](const std::shared_ptr<Bonus> & record)
		{
			result.records.push_back(offset + input.retainedRecords.size());
			input.retainedRecords.push_back(record); // Same raw alias pool as accepted native arrivals.
		});
	return result;
}
}
