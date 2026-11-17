/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "CapturedRuleRef.h"
#include <optional>

namespace nativeRules
{
/// Standalone owner snapshots are self-contained. World snapshots instead use
/// the already emitted world table. Scope restores a surrounding serializer's
/// context on both success and exceptions; no callback/world lookup is required.
template<typename Handler, typename Owner> class CapturedRuleOwnerEnvelope final
{
	CapturedRulePool local;
	CapturedRulePool * pool = nullptr;
	std::optional<CapturedRuleWireScope<Handler>> scope;
	CapturedRuleSetId identity = 0;
public:
	CapturedRuleOwnerEnvelope(Handler & h, Owner & owner)
	{
		pool = wireRulePool(h);
		bool pooled = h.saving && pool;
		h & pooled;
		if(!pooled && pool)
			throw std::runtime_error("Standalone owner envelope inside an existing rules scope");
		if(pooled && !pool)
			throw std::runtime_error("Missing captured rules table for owner reference");
		if(!pooled)
		{
			pool = &local;
			if(h.saving) owner.admitCapturedRuleSet(local);
			h & local;
		}
		scope.emplace(h, *pool);
		int64_t wireIdentity = h.saving ? owner.findCapturedRuleSet(*pool) : 0;
		h & wireIdentity;
		if(wireIdentity <= 0 || wireIdentity > 100000)
			throw std::runtime_error("Invalid captured owner identity");
		identity = static_cast<CapturedRuleSetId>(wireIdentity);
		if(!h.saving) owner.restoreCapturedRuleSet(*pool, identity);
	}
	void verify(const Owner & owner) const
	{
		if(owner.findCapturedRuleSet(*pool) != identity)
			throw std::runtime_error("Captured owner composition disagrees with component references");
	}
};
}
