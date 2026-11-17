/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "SearchBudget.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace battleSearch
{
namespace
{
size_t index(WorkKind kind)
{
	const auto value = static_cast<size_t>(kind);
	if(value >= workKindCount) throw std::invalid_argument("Invalid BattleAI work kind");
	return value;
}
size_t index(Family family)
{
	const auto value = static_cast<size_t>(family);
	if(value >= familyCount) throw std::invalid_argument("Invalid BattleAI search family");
	return value;
}
}

struct SearchBudget::Node
{
	std::shared_ptr<Node> parent;
	WorkAmounts limit;
	WorkAmounts used{};
	WorkAmounts reserved{};
	std::array<uint64_t, workKindCount> refused{};
	explicit Node(WorkAmounts limit, std::shared_ptr<Node> parent = std::shared_ptr<Node>())
		: parent(std::move(parent)), limit(limit) {}
	~Node()
	{
		// Descendants keep their ancestors alive. Therefore no outstanding child
		// reservation exists when this node finally releases its own remainder.
		if(parent)
			for(size_t i = 0; i < workKindCount; ++i)
				parent->reserved[i] -= limit[i] - used[i];
	}
	uint32_t available(size_t i) const { return limit[i] - used[i] - reserved[i]; }
};

SearchBudget::SearchBudget(WorkAmounts limits) : node(std::make_shared<Node>(limits)) {}
SearchBudget::SearchBudget(std::shared_ptr<Node> node) : node(std::move(node)) {}
SearchBudget::~SearchBudget() = default;

SearchLimits SearchLimits::full()
{
	SearchLimits result;
	result.decision = {65536, 1024, 512, 1048576, 262144};
	result.generationPerFamily = 65536;
	result.generationPerSpell = 65536;
	result.scoutsPerFamily = 1024;
	result.refinementCount = 512;
	result.referencePerFamily = 512;
	result.continuation = {4096, 0, 1, 65536, 16384};
	result.refineScouted = false;
	return result;
}

bool SearchBudget::consume(WorkKind kind, uint32_t units)
{
	const auto i = index(kind);
	if(!node) return false;
	if(units > node->available(i))
	{
		for(auto current = node; current; current = current->parent)
			if(current->refused[i] != std::numeric_limits<uint64_t>::max()) ++current->refused[i];
		return false;
	}
	node->used[i] += units;
	for(auto parent = node->parent; parent; parent = parent->parent)
	{
		parent->reserved[i] -= units;
		parent->used[i] += units;
	}
	return true;
}

void SearchBudget::require(WorkKind kind, uint32_t units)
{
	if(!consume(kind, units)) throw BudgetExhausted(kind);
}

std::optional<SearchBudget> SearchBudget::reserve(WorkAmounts limits)
{
	if(!node) return std::nullopt;
	for(size_t i = 0; i < workKindCount; ++i)
		if(limits[i] > node->available(i))
		{
			for(auto current = node; current; current = current->parent)
				if(current->refused[i] != std::numeric_limits<uint64_t>::max()) ++current->refused[i];
			return std::nullopt;
		}
	// Allocate before charging, so an allocation failure leaves the parent intact.
	auto child = std::make_shared<Node>(limits, node);
	for(size_t i = 0; i < workKindCount; ++i) node->reserved[i] += limits[i];
	return SearchBudget(std::move(child));
}

WorkAmounts SearchBudget::spent() const { return node ? node->used : WorkAmounts{}; }
std::array<uint64_t, workKindCount> SearchBudget::refusals() const
{
	return node ? node->refused : std::array<uint64_t, workKindCount>{};
}
WorkAmounts SearchBudget::available() const
{
	WorkAmounts result{};
	if(node)
		for(size_t i = 0; i < workKindCount; ++i) result[i] = node->available(i);
	return result;
}

FamilyReservations::FamilyReservations(SearchBudget * root, WorkKind kind, uint32_t maximumPerFamily, bool partition)
	: root(root), partitioned(root && partition)
{
	if(!partitioned) return;
	const auto i = index(kind);
	const auto amount = std::min<uint32_t>(maximumPerFamily, root->available()[i] / familyCount);
	WorkAmounts quota{};
	quota[i] = amount;
	for(auto & family : families) family = root->reserve(quota);
}

SearchBudget * FamilyReservations::forFamily(Family family)
{
	const auto i = index(family);
	if(!partitioned) return root;
	if(!families[i]) throw std::logic_error("Missing BattleAI family reservation");
	return &*families[i];
}

HeroWork::HeroWork(SearchBudget & root, const SearchLimits & limits) : perCandidate(limits.continuation)
{
	const auto available = root.available();
	WorkAmounts phase{};
	for(size_t i = 0; i < workKindCount; ++i)
	{
		if(i == index(WorkKind::Scout)) continue;
		if(i == index(WorkKind::Continuation)) phase[i] = available[i];
		else
		{
			const uint64_t numerator = static_cast<uint64_t>(available[i])
				* (i == index(WorkKind::Generation) ? 1 : 3);
			phase[i] = static_cast<uint32_t>(numerator / 4);
		}
	}
	continuation = root.reserve(phase);
}

std::optional<SearchBudget> HeroWork::reserveContinuation()
{
	if(!continuation) return std::nullopt;
	return continuation->reserve(perCandidate);
}

std::vector<size_t> refinementPlan(std::span<const RefinementCandidate> candidates, const SearchLimits & limits)
{
	if(!limits.refineScouted)
	{
		std::vector<size_t> result;
		for(const auto & candidate : candidates)
		{
			index(candidate.family);
			if(result.size() == limits.decision[index(WorkKind::Continuation)]) break;
			result.push_back(candidate.ordinal);
		}
		return result;
	}
	std::array<uint32_t, familyCount> references{};
	std::vector<const RefinementCandidate *> known;
	std::vector<const RefinementCandidate *> unknown;
	for(const auto & candidate : candidates)
	{
		index(candidate.family);
		if(candidate.scoutValue && std::isfinite(*candidate.scoutValue)) known.push_back(&candidate);
		else unknown.push_back(&candidate);
	}

	const auto ordinal = [](auto left, auto right) { return left->ordinal < right->ordinal; };
	std::stable_sort(unknown.begin(), unknown.end(), ordinal);
	std::stable_sort(known.begin(), known.end(), [&](auto left, auto right)
	{
		if(*left->scoutValue != *right->scoutValue) return *left->scoutValue > *right->scoutValue;
		return ordinal(left, right);
	});
	std::vector<size_t> result;
	const auto cap = limits.decision[index(WorkKind::Continuation)];
	// Round-robin when configured for more than one representative per family.
	for(size_t round = 0; round < std::min<size_t>(limits.referencePerFamily, unknown.size()) && result.size() < cap; ++round)
		for(size_t family = 0; family < familyCount && result.size() < cap; ++family)
			for(const auto * candidate : unknown)
				if(index(candidate->family) == family && references[family] <= round
					&& std::find(result.begin(), result.end(), candidate->ordinal) == result.end())
				{
					result.push_back(candidate->ordinal);
					++references[family];
					break;
				}
	uint32_t refinements = 0;
	for(const auto * candidate : known)
	{
		if(result.size() >= cap || refinements >= limits.refinementCount) break;
		if(std::find(result.begin(), result.end(), candidate->ordinal) != result.end()) continue;
		result.push_back(candidate->ordinal);
		++refinements;
	}
	std::sort(result.begin(), result.end());
	return result;
}

std::vector<size_t> generationOrder(std::span<const Family> families)
{
	std::array<std::vector<size_t>, familyCount> groups;
	for(size_t ordinal = 0; ordinal < families.size(); ++ordinal)
		groups[index(families[ordinal])].push_back(ordinal);
	std::vector<size_t> result;
	result.reserve(families.size());
	for(size_t round = 0; result.size() != families.size(); ++round)
		for(const auto & group : groups)
			if(round < group.size()) result.push_back(group[round]);
	return result;
}

Family spellFamily(std::string_view key, bool damage, bool positive, bool rising)
{
	if(key == "core:teleport" || key == "new-horizons:blink" || key == "new-horizons:forcedRecall"
		|| key == "new-horizons:realityWarp") return Family::Movement;
	if(key == "core:summonFireElemental" || key == "core:summonWaterElemental"
		|| key == "core:summonAirElemental" || key == "core:summonEarthElemental"
		|| key == "new-horizons:summonTrolls" || key == "new-horizons:summonLivingArmor"
		|| key == "new-horizons:elementalConvergence" || key == "new-horizons:phantomArmy"
		|| key == "core:clone" || key == "new-horizons:verdantPrison") return Family::Summon;
	if(rising || key == "core:cure" || key == "core:resurrection" || key == "core:sacrifice"
		|| key == "new-horizons:lifeDrain"
		|| key == "core:animateDead" || key == "new-horizons:reanimate"
		|| key == "new-horizons:regeneration" || key == "new-horizons:purify") return Family::Healing;
	if(key == "core:fireWall" || key == "core:landMine" || key == "core:quicksand"
		|| key == "core:forceField" || key == "core:removeObstacle"
		|| key == "new-horizons:transfigureMatter" || key == "new-horizons:vengefulVines") return Family::Obstacle;
	if(damage) return Family::Damage;
	return positive ? Family::Enhancement : Family::Control;
}
}
