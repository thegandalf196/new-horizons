/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../Global.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace battleSearch
{
enum class WorkKind : uint8_t { Generation, Scout, Continuation, Node, Movement, Count };
constexpr size_t workKindCount = static_cast<size_t>(WorkKind::Count);
using WorkAmounts = std::array<uint32_t, workKindCount>;

/// Performance policy only. These groups never determine effect legality.
enum class Family : uint8_t { Damage, Healing, Control, Summon, Movement, Obstacle, Enhancement, Orders, Count };
constexpr size_t familyCount = static_cast<size_t>(Family::Count);

/// Central, experimental work policy. Full preserves ordinary exact search
/// until a larger finite safety ceiling. Null is a legacy diagnostic only.
struct DLL_LINKAGE SearchLimits
{
	WorkAmounts decision{8192, 128, 12, 12288, 4096};
	uint32_t generationPerFamily = 1024;
	uint32_t generationPerSpell = 128;
	uint32_t scoutsPerFamily = 16;
	uint32_t refinementCount = 3;
	uint32_t referencePerFamily = 1;
	WorkAmounts continuation{128, 0, 1, 768, 256};
	bool refineScouted = true;
	static SearchLimits full();
};

class DLL_LINKAGE BudgetExhausted final : public std::exception
{
	WorkKind kind;
public:
	explicit BudgetExhausted(WorkKind kind) noexcept : kind(kind) {}
	const char * what() const noexcept override { return "BattleAI deterministic search budget exhausted"; }
	WorkKind workKind() const noexcept { return kind; }
};

/// One synchronous decision, with explicit nested reservations. A reservation
/// shields its remaining quota from siblings; spent work is charged to every
/// ancestor. Unused work returns only when the last child handle is destroyed.
/// No wall clock, global state, RNG, callbacks, gameplay state or serialization.
class DLL_LINKAGE SearchBudget
{
	struct Node;
	std::shared_ptr<Node> node;
	explicit SearchBudget(std::shared_ptr<Node> node);
public:
	explicit SearchBudget(WorkAmounts limits);
	SearchBudget(SearchBudget &&) noexcept = default;
	SearchBudget & operator=(SearchBudget &&) noexcept = default;
	SearchBudget(const SearchBudget &) = delete;
	SearchBudget & operator=(const SearchBudget &) = delete;
	~SearchBudget();
	bool consume(WorkKind kind, uint32_t units = 1);
	void require(WorkKind kind, uint32_t units = 1);
	std::optional<SearchBudget> reserve(WorkAmounts limits);
	WorkAmounts spent() const;
	WorkAmounts available() const;
	std::array<uint64_t, workKindCount> refusals() const;
};

/// Explicit cooperative boundary; null is an opt-in unbudgeted diagnostic.
inline void require(SearchBudget * budget, WorkKind kind, uint32_t units = 1)
{
	if(budget) budget->require(kind, units);
}

/// Protect discovery/scout work for every family before the first family runs.
/// Full can retain historical traversal through one shared finite root.
class DLL_LINKAGE FamilyReservations
{
	SearchBudget * root;
	std::array<std::optional<SearchBudget>, familyCount> families;
	bool partitioned;
public:
	FamilyReservations(SearchBudget * root, WorkKind kind, uint32_t maximumPerFamily, bool partition);
	SearchBudget * forFamily(Family family);
};

/// Reserve continuation work before discovery/scouting. The fraction is a phase
/// ceiling inside the same decision total, never a reset of consumed counters.
class DLL_LINKAGE HeroWork
{
	std::optional<SearchBudget> continuation;
	WorkAmounts perCandidate;
public:
	HeroWork(SearchBudget & root, const SearchLimits & limits);
	std::optional<SearchBudget> reserveContinuation();
};

struct RefinementCandidate
{
	size_t ordinal;
	Family family;
	/// Missing means unscoutable, not zero utility or illegal.
	std::optional<float> scoutValue;
};

/// Reserve a reference representative for each unscoutable native family, then
/// take the strongest supported scouts. Ties and final work order use original
/// ordinals. Unselected candidates have no evaluated utility.
DLL_LINKAGE std::vector<size_t> refinementPlan(std::span<const RefinementCandidate> candidates,
	const SearchLimits & limits = SearchLimits());

/// Round-robin stable family order for discovery: one member per family per
/// pass. Does not choose targets, mutate state or spend work by itself.
DLL_LINKAGE std::vector<size_t> generationOrder(std::span<const Family> families);
/// Native metadata grouping solely for fair work reservations. Unknown neutral
/// effects retain the control reference group, never an unlimited bypass.
DLL_LINKAGE Family spellFamily(std::string_view key, bool damage, bool positive, bool rising);
}
