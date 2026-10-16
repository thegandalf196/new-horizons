/*
 * PreparedBonusGraphStorage.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CBonusSystemNode.h"
#include <optional>

/// Owns original/proposed graph storage, not the borrowed graph nodes themselves.
/// A future world owner must hold node lifetimes and exclude competing graph
/// access from capture through commit, including readers during reservation.
/// Shared Bonus values and transferred-container aliases must remain immutable.
/// This does NOT evaluate propagation, validate complete graph semantics or apply
/// packets. Invalidation targets must be the planner's exact ordered closures.
class DLL_LINKAGE PreparedBonusGraphStorage final : boost::noncopyable
{
public:
	struct DLL_LINKAGE Values
	{
		BonusList accepted;
		BonusList exported;
		TCNodesVector parents;
		TNodesVector propagatingParents;
		TNodesVector children;
	};

	PreparedBonusGraphStorage() = default;
	PreparedBonusGraphStorage(const PreparedBonusGraphStorage &) = delete;
	PreparedBonusGraphStorage & operator=(const PreparedBonusGraphStorage &) = delete;
	PreparedBonusGraphStorage(PreparedBonusGraphStorage &&) = delete;
	PreparedBonusGraphStorage & operator=(PreparedBonusGraphStorage &&) = delete;
	size_t capture(CBonusSystemNode & node);
	const Values & projected(size_t index) const;
	void replace(size_t index, Values replacement);
	/// false retains the current generation, matching propagation inside an
	/// already-started legacy event; true starts the next generation. Leading
	/// false events borrow prepare's exact generation, never silently advance.
	void addInvalidation(bool advanceGeneration, std::vector<CBonusSystemNode *> targets);
	/// Exact journal binding; default callers retain the existing rebase policy.
	void requireGeneration(int32_t generation);
	static int32_t currentGeneration() noexcept;
	/// May allocate storage capacity, never changes logical live graph values.
	void prepare();
	bool stillCurrent() const noexcept;
	bool readyToCommit() const noexcept;
	/// Refuses stale/unprepared/reused handles BEFORE any write. After prepare,
	/// the owning world must retain its mutation lease before publication.
	/// Validated generations are CAS-reserved before values change; counter races
	/// refuse without a reservation, while successful reservations are not rolled back.
	bool commit() noexcept;

private:
	friend class ProjectedBonusGraph;
	friend class ProjectedBonusInvalidations;
	enum class Phase { BUILDING, PREPARED, COMMITTED };
	struct Entry
	{
		CBonusSystemNode * node;
		int32_t version;
		int32_t cachedVersion;
		Values original;
		Values replacement;
	};
	struct Invalidation
	{
		bool advance;
		std::vector<CBonusSystemNode *> targets;
	};
	std::vector<Entry> entries;
	std::vector<Invalidation> invalidations;
	Phase phase = Phase::BUILDING;
	int32_t preparedGeneration = 0;
	std::optional<int32_t> requiredGeneration;

	static Values read(const CBonusSystemNode & node);
	static bool sameBonuses(const BonusList & first, const BonusList & second) noexcept;
	static void validate(const Values & value);
	void requireBuilding() const;
	bool captured(const CBonusSystemNode * node) const noexcept;
	bool payloadChanged(const Entry & entry) const noexcept;
	bool borrowsGeneration() const noexcept;
	bool validProgram(int32_t generation) const noexcept;
	int32_t finalGeneration(int32_t generation) const noexcept;
	static int32_t nextGeneration(int32_t generation) noexcept;
};
