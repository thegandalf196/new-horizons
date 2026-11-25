/*
 * ProjectedAdjacencyMemo.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class BonusList;
struct Bonus;
class CStack;
class StackWithBonuses;
namespace battle { class Unit; class CommittedBonusOperations; }

/// Visible hero specialty records (adjacency auras, Pasis) that the AI
/// re-evaluates for every projected unit.
struct ProjectedAdjacencySources
{
	std::vector<std::shared_ptr<Bonus>> records;
};

/// Selector-independent part of one projected adjacency query: the unit's
/// recomputed native baseline. Null `records` is the "no projection" answer.
struct ProjectedAdjacencyBaseline
{
	std::shared_ptr<const BonusList> records;
	bool veteranCohesionEarned = false;
	/// Native list the baseline was derived from; its records keep their identity.
	std::shared_ptr<const BonusList> native;
};

/// Branch-wide inputs of a baseline besides the roster (read by the caller).
struct ProjectedAdjacencyBranchFacts
{
	const CStack * native = nullptr;
	std::shared_ptr<const ProjectedAdjacencySources> sources;
	std::shared_ptr<const battle::CommittedBonusOperations> operations;
	int32_t treeVersion = 0;
	int32_t round = 0;
	int32_t activeStack = -1;
};

/// Never-saved, per-branch memo of ProjectedAdjacencyBonuses baselines. Its
/// key holds every value a baseline reads (branch facts, the unit's identity and
/// the position, life, owner, form, count and bonus version of every
/// roster unit) plus an epoch that branch mutators advance. A copied or assigned
/// slot starts empty, so a memo never crosses branches. Records the original
/// query created afresh are copied afresh on reuse, preserving bonus identity.
/// NH_AI_ADJACENCY_MEMO_CHECK=1 recomputes every reused answer and aborts on a
/// mismatch; NH_AI_ADJACENCY_MEMO=0 turns reuse off.
class ProjectedAdjacencyMemoSlot
{
public:
	using Compute = std::function<ProjectedAdjacencyBaseline()>;
	ProjectedAdjacencyMemoSlot();
	ProjectedAdjacencyMemoSlot(const ProjectedAdjacencyMemoSlot &) noexcept;
	ProjectedAdjacencyMemoSlot & operator=(const ProjectedAdjacencyMemoSlot &) noexcept;
	~ProjectedAdjacencyMemoSlot();
	/// A branch mutation that may change a unit's bonuses, form or adjacency.
	void invalidate() const noexcept { ++epoch; }
	ProjectedAdjacencyBaseline lookup(const StackWithBonuses & unit, const ProjectedAdjacencyBranchFacts & branch,
		const std::vector<const battle::Unit *> & roster, const Compute & compute) const;
private:
	struct State;
	mutable std::unique_ptr<State> state;
	mutable uint64_t epoch = 0;
};
