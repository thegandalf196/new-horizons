/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "CapturedRuleSet.h"
#include "../spells/NewHorizonsMetamagicDoubleCast.h"
#include "../entities/hero/NewHorizonsRevisedSpecialties.h"
#include "../entities/hero/NewHorizonsPerkRules.h"
#include <vector>
#include <map>
#include "../constants/EntityIdentifiers.h"
#include <stdexcept>

namespace nativeRules
{
/// Owned by one world or standalone snapshot envelope. Mutations occur only at
/// capture/import boundaries; queries return immutable admitted definitions.
class DLL_LINKAGE CapturedRulePool final
{
	std::vector<std::shared_ptr<const CapturedRuleDocument>> documents;
	std::vector<CapturedRuleSet> ruleSets;
	// Capture-boundary projections only; never consulted or mutated by queries.
	std::map<std::pair<CapturedDocumentId, HeroClassID>, CapturedDocumentId> classCapabilities;
	std::map<std::pair<CapturedDocumentId, HeroClassID>, CapturedDocumentId> classDevelopment;
	void validateSet(const CapturedRuleSet & set) const;
public:
	CapturedDocumentId intern(CapturedRuleKind kind, const JsonNode & document);
	CapturedDocumentId adopt(std::shared_ptr<const CapturedRuleDocument> document);
	CapturedDocumentId find(CapturedRuleKind kind, const JsonNode & document) const;
	CapturedDocumentId find(const CapturedRuleDocument & document) const;
	CapturedRuleSetId intern(const CapturedRuleSet & composition);
	CapturedRuleSetId find(const CapturedRuleSet & composition) const;
	std::shared_ptr<const CapturedRuleDocument> captureCapability(CapturedDocumentId world, HeroClassID heroClass);
	std::shared_ptr<const CapturedRuleDocument> captureDevelopment(CapturedDocumentId world, HeroClassID heroClass);
	const std::shared_ptr<const CapturedRuleDocument> & document(CapturedDocumentId id, CapturedRuleKind expected) const;
	const CapturedRuleSet & ruleSet(CapturedRuleSetId id) const;
	size_t documentCount() const { return documents.size(); }
	size_t ruleSetCount() const { return ruleSets.size(); }

	/// Decode and compile completely before replacing this pool. The enclosing
	/// snapshot emits this table before any map/hero references are decoded.
	template<typename Handler> void serialize(Handler & h)
	{
		int64_t count = static_cast<int64_t>(documents.size());
		h & count;
		if(count < 0 || count > 100000)
			throw std::runtime_error("Captured rules document table exceeds admission limit");
		CapturedRulePool candidate;
		for(int64_t index = 0; index < count; ++index)
		{
			int64_t wireKind = h.saving ? static_cast<int64_t>(documents[index]->kind()) : 0;
			h & wireKind;
			if(wireKind < 0 || wireKind > static_cast<int64_t>(CapturedRuleKind::WorldMagic))
				throw std::runtime_error("Unknown captured rule document kind");
			const auto kind = static_cast<CapturedRuleKind>(wireKind);
			const bool currentPolicies = h.hasFeature(Handler::Version::NEW_HORIZONS_DOUBLE_CAST_CHOSEN_PRAYERS);
			const bool revisedSpecialties = h.hasFeature(Handler::Version::NEW_HORIZONS_REVISED_SPECIALTIES);
			const bool sylvanAmbush = h.hasFeature(Handler::Version::NEW_HORIZONS_SYLVAN_AMBUSH);
			const bool warcast = h.hasFeature(Handler::Version::NEW_HORIZONS_WARCAST);
			if(h.saving)
			{
				if(!sylvanAmbush && newHorizonsHeroes::requiresSylvanAmbushFormat(documents[index]->document()))
					throw std::runtime_error("Cannot write Sylvan Tactics 5 rules to an older format");
				if(!warcast && newHorizonsHeroes::requiresWarcastFormat(documents[index]->document()))
					throw std::runtime_error("Cannot write Warcasting rules to an older format");
				if(!currentPolicies && newHorizonsMetamagic::requiresDoubleCastFormat(documents[index]->document()))
					throw std::runtime_error("Cannot write Double Cast or Chosen Prayers rules to an older format");
				if(!revisedSpecialties && newHorizonsHeroes::requiresRevisedSpecialtyFormat(documents[index]->document()))
					throw std::runtime_error("Cannot write revised specialty rules to an older format");
				h & documents[index]->document();
			}
			else
			{
				JsonNode decoded;
				h & decoded;
				if(!currentPolicies && newHorizonsMetamagic::requiresDoubleCastFormat(decoded))
					throw std::runtime_error("Older format claims Double Cast or Chosen Prayers rules");
				if(!revisedSpecialties && newHorizonsHeroes::requiresRevisedSpecialtyFormat(decoded))
					throw std::runtime_error("Older format claims revised specialty rules");
				if(!sylvanAmbush && newHorizonsHeroes::requiresSylvanAmbushFormat(decoded))
					throw std::runtime_error("Older format claims Sylvan Tactics 5 rules");
				if(!warcast && newHorizonsHeroes::requiresWarcastFormat(decoded))
					throw std::runtime_error("Older format claims Warcasting rules");
				if(candidate.intern(kind, decoded) != index + 1)
					throw std::runtime_error("Duplicate captured rules document identity");
			}
		}
		int64_t setCount = static_cast<int64_t>(ruleSets.size());
		h & setCount;
		if(setCount < 0 || setCount > 100000)
			throw std::runtime_error("Captured rule composition table exceeds admission limit");
		for(int64_t index = 0; index < setCount; ++index)
		{
			if(h.saving) h & ruleSets[index];
			else
			{
				CapturedRuleSet composition;
				h & composition;
				if(candidate.intern(composition) != index + 1)
					throw std::runtime_error("Duplicate captured rule composition identity");
			}
		}
		if(!h.saving) *this = std::move(candidate);
	}
};

/// Full equality includes JSON metadata: JsonNode::operator== alone excludes
/// identifier scopes and override flags and cannot establish capture identity.
DLL_LINKAGE bool equalCapturedDocuments(const JsonNode & lhs, const JsonNode & rhs);
}
