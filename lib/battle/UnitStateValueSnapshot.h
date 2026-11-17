/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../Global.h"
#include "BattleSide.h"
#include "../constants/EntityIdentifiers.h"
#include <memory>
#include <optional>
namespace battle
{
class CUnitState;
/// Ephemeral owned native value ledger. No serialization, provider/environment,
/// Unit pointer, bonus-query cache, callable or RNG is retained. Copies share immutable data.
class DLL_LINKAGE UnitStateValueSnapshot final
{
public:
	struct Identity
	{
		uint32_t id;
		CreatureID originalCreature;
		int32_t baseAmount;
		BattleSide side;
		PlayerColor owner;
		SlotID slot;
		bool operator==(const Identity &) const = default;
	};
	struct Status
	{
		int32_t count;
		CreatureID effectiveCreature;
		bool ghost, summoned, natureSummoned, hasForm;
	};
	/// Validation metadata from the SAME canonical capture, not mutable state.
	Status status() const;
	static UnitStateValueSnapshot capture(const CUnitState & source);
	Identity identity() const;
	/// Raw observation history, without evaluating/latching native total().
	int32_t retaliationCache() const;
	static int32_t retaliationCache(const CUnitState &);
	/// Cheap ephemeral overlay: shares the complete immutable inventory.
	UnitStateValueSnapshot withRetaliationCache(int32_t) const;
	/// Preparation-only: receiver descriptor/bonus/environment must already
	/// match this snapshot. Prevalidates all ownership/health contexts and
	/// allocates before inventory-ordered nonthrowing value swaps. It transfers
	/// no proxy/query-cache and invokes no form callback or gameplay publication.
	void restore(CUnitState & receiver) const;
private:
	class Data;
	std::shared_ptr<const Data> data;
	std::optional<int32_t> retaliationCacheOverride;
	explicit UnitStateValueSnapshot(std::shared_ptr<const Data> data);
};
}
