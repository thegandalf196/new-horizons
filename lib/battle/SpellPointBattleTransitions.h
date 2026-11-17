/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../entities/hero/SpellPointState.h"
#include <optional>
namespace battle::spellPoints
{
struct DLL_LINKAGE Payment
{
	int32_t normal = 0, permanentBuffer = 0, temporaryBuffer = 0;
	int64_t total() const { return static_cast<int64_t>(normal) + permanentBuffer + temporaryBuffer; }
	bool operator==(const Payment &) const = default;
	void validate() const;
};
/// Owned values only. Capacity is a captured current hero fact, not a policy.
struct DLL_LINKAGE State
{
	newHorizonsHeroes::SpellPointState pools;
	int32_t temporaryBufferRemaining = 0;
	int32_t normalCapacity = 0;
	bool twoPools = false;
	int32_t normalLimit() const { return twoPools ? normalCapacity : std::numeric_limits<int32_t>::max(); }
	void validate() const;
	bool operator==(const State & other) const;
};
/// Stage on a value copy; rejected input cannot publish a partial pool update.
DLL_LINKAGE std::optional<Payment> spend(State & state, int64_t amount);
/// Restore explicit charge provenance; all pool headroom is checked first.
DLL_LINKAGE bool refund(State & state, Payment payment);
/// Validate cumulative refunds against one original charge before restoration.
DLL_LINKAGE bool refundCharge(State & state, Payment original, Payment & refunded, Payment payment);
}
