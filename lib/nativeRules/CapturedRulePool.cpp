/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CapturedRulePool.h"
#include <limits>

namespace nativeRules
{
bool equalCapturedDocuments(const JsonNode & lhs, const JsonNode & rhs)
{
	if(lhs.getType() != rhs.getType() || lhs.getModScope() != rhs.getModScope()
		|| lhs.getOverrideFlag() != rhs.getOverrideFlag()) return false;
	if(lhs.isStruct())
	{
		if(lhs.Struct().size() != rhs.Struct().size()) return false;
		auto right = rhs.Struct().begin();
		for(const auto & [key, value] : lhs.Struct())
		{
			if(key != right->first || !equalCapturedDocuments(value, right->second)) return false;
			++right;
		}
		return true;
	}
	if(lhs.isVector())
	{
		if(lhs.Vector().size() != rhs.Vector().size()) return false;
		for(size_t index = 0; index < lhs.Vector().size(); ++index)
			if(!equalCapturedDocuments(lhs[index], rhs[index])) return false;
		return true;
	}
	return lhs == rhs;
}

CapturedDocumentId CapturedRulePool::find(CapturedRuleKind kind, const JsonNode & source) const
{
	for(size_t index = 0; index < documents.size(); ++index)
		if(documents[index]->kind() == kind && (&documents[index]->document() == &source
			|| equalCapturedDocuments(documents[index]->document(), source)))
			return static_cast<CapturedDocumentId>(index + 1);
	throw std::runtime_error("Unregistered captured rules document");
}

CapturedDocumentId CapturedRulePool::find(const CapturedRuleDocument & source) const
{
	for(size_t index = 0; index < documents.size(); ++index)
		if(documents[index].get() == &source || (source.perks() && documents[index]->perks() == source.perks()))
			return static_cast<CapturedDocumentId>(index + 1);
	return find(source.kind(), source.document());
}

CapturedDocumentId CapturedRulePool::intern(CapturedRuleKind kind, const JsonNode & source)
{
	for(size_t index = 0; index < documents.size(); ++index)
		if(documents[index]->kind() == kind && (&documents[index]->document() == &source
			|| equalCapturedDocuments(documents[index]->document(), source)))
			return static_cast<CapturedDocumentId>(index + 1);
	return adopt(CapturedRuleDocument::compile(kind, source));
}

CapturedDocumentId CapturedRulePool::adopt(std::shared_ptr<const CapturedRuleDocument> source)
{
	if(!source) throw std::runtime_error("Missing captured rules owner");
	for(size_t index = 0; index < documents.size(); ++index)
		if(documents[index] == source || (source->perks() && documents[index]->perks() == source->perks())
			|| (documents[index]->kind() == source->kind()
			&& equalCapturedDocuments(documents[index]->document(), source->document())))
			return static_cast<CapturedDocumentId>(index + 1);
	if(documents.size() >= 100000) throw std::runtime_error("Captured rules table exhausted");
	documents.push_back(std::move(source));
	return static_cast<CapturedDocumentId>(documents.size());
}

const std::shared_ptr<const CapturedRuleDocument> & CapturedRulePool::document(CapturedDocumentId id, CapturedRuleKind expected) const
{
	if(id == 0 || id > documents.size() || documents[id - 1]->kind() != expected)
		throw std::runtime_error("Invalid captured rule document reference");
	return documents[id - 1];
}

void CapturedRulePool::validateSet(const CapturedRuleSet & set) const
{
	constexpr std::array kinds{CapturedRuleKind::Mastery, CapturedRuleKind::Perks,
		CapturedRuleKind::ResolvedCapability, CapturedRuleKind::ResolvedDevelopment};
	for(size_t index = 0; index < kinds.size(); ++index)
		document(set.documents[index], kinds[index]);
}

CapturedRuleSetId CapturedRulePool::intern(const CapturedRuleSet & composition)
{
	validateSet(composition);
	for(size_t index = 0; index < ruleSets.size(); ++index)
		if(ruleSets[index] == composition) return static_cast<CapturedRuleSetId>(index + 1);
	if(ruleSets.size() >= 100000) throw std::runtime_error("Captured rule compositions exhausted");
	ruleSets.push_back(composition);
	return static_cast<CapturedRuleSetId>(ruleSets.size());
}

CapturedRuleSetId CapturedRulePool::find(const CapturedRuleSet & composition) const
{
	for(size_t index = 0; index < ruleSets.size(); ++index)
		if(ruleSets[index] == composition) return static_cast<CapturedRuleSetId>(index + 1);
	throw std::runtime_error("Unregistered captured rule composition");
}

const CapturedRuleSet & CapturedRulePool::ruleSet(CapturedRuleSetId id) const
{
	if(id == 0 || id > ruleSets.size()) throw std::runtime_error("Invalid captured rule set reference");
	return ruleSets[id - 1];
}
}
