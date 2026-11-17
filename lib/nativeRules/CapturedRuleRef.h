/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "CapturedRulePool.h"
#include "CapturedRuleWireContext.h"
#include <utility>

namespace nativeRules
{
template<typename Handler> class CapturedRuleWireScope final
{
	Handler & handler;
	CapturedRulePool * previous = nullptr;
public:
	CapturedRuleWireScope(Handler & handler, CapturedRulePool & pool) : handler(handler)
	{
		if constexpr(requires { handler.capturedRules.pool; })
		{
			previous = handler.capturedRules.pool;
			handler.capturedRules.pool = &pool;
		}
	}
	~CapturedRuleWireScope()
	{
		if constexpr(requires { handler.capturedRules.pool; })
			handler.capturedRules.pool = previous;
	}
	CapturedRuleWireScope(const CapturedRuleWireScope &) = delete;
	CapturedRuleWireScope & operator=(const CapturedRuleWireScope &) = delete;
};

template<typename Handler> CapturedRulePool * wireRulePool(Handler & handler)
{
	if constexpr(requires { handler.capturedRules.pool; }) return handler.capturedRules.pool;
	return nullptr;
}

/// Immutable document facade. Replacement validates a complete candidate first.
/// Const JSON compatibility allows cold/UI readers to migrate by domain while
/// exposing no mutation capable of invalidating typed projections.
template<CapturedRuleKind Kind> class CapturedRuleRef final
{
	std::shared_ptr<const CapturedRuleDocument> captured;
public:
	CapturedRuleRef() = default;
	explicit CapturedRuleRef(const JsonNode & node) { replace(node); }
	CapturedRuleRef & operator=(JsonNode node) { replace(std::move(node)); return *this; }
	void replace(JsonNode node) { captured = CapturedRuleDocument::compile(Kind, std::move(node)); }
	void bind(std::shared_ptr<const CapturedRuleDocument> document)
	{
		if(!document || document->kind() != Kind) throw std::runtime_error("Captured rule component type mismatch");
		captured = std::move(document);
	}
	const std::shared_ptr<const CapturedRuleDocument> & owner() const { return captured; }
	const CompiledDevelopmentRules * development() const { return captured ? captured->development().get() : nullptr; }
	const CompiledMasteryRules * mastery() const { return captured ? captured->mastery().get() : nullptr; }
	std::optional<std::array<newHorizonsHeroes::MasteryOption, 3>> masteryOptions(SecondarySkill skill) const
	{
		const auto * rules = mastery();
		return rules ? rules->options(skill) : std::nullopt;
	}
	const JsonNode & document() const
	{
		static const JsonNode empty;
		return captured ? captured->document() : empty;
	}
	operator const JsonNode &() const { return document(); }
	friend bool operator==(const CapturedRuleRef & lhs, const CapturedRuleRef & rhs)
	{
		return equalCapturedDocuments(lhs.document(), rhs.document());
	}
	const JsonNode & operator[](const std::string & key) const { return document()[key]; }
	friend bool operator==(const CapturedRuleRef & lhs, const JsonNode & rhs)
	{
		return equalCapturedDocuments(lhs.document(), rhs);
	}
	friend bool operator==(const JsonNode & lhs, const CapturedRuleRef & rhs) { return rhs == lhs; }
	JsonNode::JsonType getType() const { return document().getType(); }
	bool isNull() const { return document().isNull(); }
	bool isStruct() const { return document().isStruct(); }
	const JsonMap & Struct() const { return document().Struct(); }

	template<typename Handler> void serialize(Handler & h)
	{
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_SHARED_CAPTURED_RULES))
		{
			if(h.saving) h & document();
			else
			{
				JsonNode decoded;
				h & decoded;
				replace(std::move(decoded));
			}
			return;
		}
		auto * pool = wireRulePool(h);
		bool pooled = h.saving && pool;
		h & pooled;
		if(!pooled && pool)
			throw std::runtime_error("Inline captured rules inside an existing table scope");
		if(pooled)
		{
			if(!pool) throw std::runtime_error("Missing captured rules table for document reference");
			int64_t id = h.saving ? (captured ? pool->find(*captured) : pool->find(Kind, document())) : 0;
			h & id;
			if(id <= 0 || id > 100000) throw std::runtime_error("Invalid captured document identity");
			if(!h.saving) bind(pool->document(static_cast<CapturedDocumentId>(id), Kind));
		}
		else if(h.saving)
			h & document();
		else
		{
			JsonNode decoded;
			h & decoded;
			replace(std::move(decoded));
		}
	}
};
}
