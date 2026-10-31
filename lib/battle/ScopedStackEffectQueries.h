/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "CUnitState.h"
#include "AccessibilityInfo.h"
#include "IBattleInfoCallback.h"
#include "../bonuses/BonusList.h"
#include <memory>
#include <vector>

class BattleInfo;
class CStack;
class ProjectedBonusRoster;
namespace battle
{
/// Synchronous native-authoritative dry run. Caller retains the actual state,
/// world and initialized library until destruction, on the constructing thread.
/// Acquires native graph access AFTER caller state access. Never use this to
/// import a player/projected source graph; those require a ledger-owned adapter.
/// All returned state/list/Bonus observations borrow this scope, READ-ONLY except
/// state() and the named export operations. No native graph/cache writes occur.
class DLL_LINKAGE ScopedStackEffectQueries final : boost::noncopyable
{
	struct Data;
	std::unique_ptr<Data> data;
	static Units adjacentGeometry(const ProjectedBonusRoster &, const Unit &);
public:
	ScopedStackEffectQueries(const BattleInfo & battle, const std::vector<const CStack *> & targets);
	~ScopedStackEffectQueries();
	const Unit & current(uint32_t id) const;
	/// Unique current occurrence across the full native roster; null if absent/ambiguous.
	/// Read-only lookup does not grant mutation entitlement for a new target.
	const Unit * find(uint32_t id) const;
	CUnitState & state(uint32_t id);
	const BonusList & exportedRecords(uint32_t id) const;
	void appendExported(uint32_t id, const std::shared_ptr<Bonus> & value);
	void removeExported(uint32_t id, const std::shared_ptr<Bonus> & value);
	void removeMatchingExports(uint32_t id, const CSelector & selector);
	/// Exact local matching refreshFields only, preserving value/caster/source.
	void refreshMatchingExports(uint32_t id, const Bonus & value);
	AccessibilityInfo accessibility(uint32_t id) const;
};
}
