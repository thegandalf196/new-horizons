/*
 * ProjectedAdjacencyMemo.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedAdjacencyMemo.h"
#include "StackWithBonuses.h"
#include "../../lib/CStack.h"
#include "../../lib/bonuses/BonusList.h"
#include <cstdlib>
#include <set>
#include <string_view>
#include <unordered_map>

namespace
{
/// Everything an adjacency, creature or count-dependent limiter or updater can
/// read from one roster unit, by value; the pointer keeps the roster order exact.
/// Only bonus-free state: health and other derived values query bonuses, which
/// would re-enter this memo.
struct UnitFacts
{
	const battle::Unit * unit = nullptr;
	uint32_t id = 0;
	BattleHex position;
	bool alive = false;
	bool ghost = false;
	bool doubleWide = false;
	BattleSide side = BattleSide::NONE;
	PlayerColor owner = PlayerColor::CANNOT_DETERMINE;
	CreatureID creature = CreatureID::NONE;
	const CCreature * type = nullptr;
	int32_t count = 0;
	int32_t treeVersion = 0;
	bool operator==(const UnitFacts &) const = default;
};

std::vector<UnitFacts> rosterFacts(const std::vector<const battle::Unit *> & roster)
{
	std::vector<UnitFacts> result;
	result.reserve(roster.size());
	for(const auto * unit : roster)
	{
		if(!unit)
		{
			result.emplace_back();
			continue;
		}
		result.push_back({unit, unit->unitId(), unit->getPosition(), unit->alive(), unit->isGhost(), unit->doubleWide(),
			unit->unitSide(), unit->unitOwner(), unit->creatureId(), unit->unitType(), unit->getCount(),
			unit->getTreeVersion()});
	}
	return result;
}

bool sameBranch(const ProjectedAdjacencyBranchFacts & left, const ProjectedAdjacencyBranchFacts & right)
{
	return left.native == right.native && left.sources == right.sources && left.operations == right.operations
		&& left.treeVersion == right.treeVersion && left.round == right.round && left.activeStack == right.activeStack;
}

bool sameValue(const Bonus & left, const Bonus & right)
{
	return left.duration == right.duration && left.val == right.val && left.turnsRemain == right.turnsRemain
		&& left.valType == right.valType && left.source == right.source && left.targetSourceType == right.targetSourceType
		&& left.effectRange == right.effectRange && left.type == right.type && left.subtype == right.subtype
		&& left.sid == right.sid && left.stacking == right.stacking && left.parameters == right.parameters
		&& left.limiter == right.limiter && left.propagator == right.propagator && left.updater == right.updater
		&& left.propagationUpdater == right.propagationUpdater && left.bonusOwner == right.bonusOwner
		&& left.appliedByEnemy == right.appliedByEnemy && left.spellCasterOwner == right.spellCasterOwner
		&& left.statusIdentity == right.statusIdentity && left.hidden == right.hidden;
}

/// Positions of records the original query creates afresh on every call:
/// neither a native node record nor an authorized source record. Also stacks
/// the baseline once and finds the same records among the stacked ones.
void prepare(ProjectedAdjacencyBaseline & value, const ProjectedAdjacencySources * sources)
{
	value.fresh.clear();
	value.freshStacked.clear();
	value.stacked.reset();
	if(!value.records)
		return;
	std::set<const Bonus *> stable;
	if(value.native)
		for(const auto & record : *value.native)
			stable.insert(record.get());
	if(sources)
		for(const auto & record : sources->records)
			stable.insert(record.get());
	std::set<const Bonus *> fresh;
	for(size_t index = 0; index < value.records->size(); ++index)
		if(!stable.contains((*value.records)[index].get()))
		{
			value.fresh.push_back(index);
			fresh.insert((*value.records)[index].get());
		}
	auto stacked = std::make_shared<BonusList>(*value.records);
	stacked->stackBonuses();
	for(size_t index = 0; index < stacked->size(); ++index)
		if(fresh.contains((*stacked)[index].get()))
			value.freshStacked.push_back(index);
	value.stacked = stacked;
}

/// Checker mode: one list of a reused answer must equal a fresh computation,
/// record by record (identity for stable records, value for fresh ones).
std::string listMismatch(const BonusList & reused, const BonusList & computed, const std::vector<size_t> & fresh)
{
	if(reused.size() != computed.size())
		return "record count differs";
	for(size_t index = 0, next = 0; index < computed.size(); ++index)
	{
		const bool isFresh = next < fresh.size() && fresh[next] == index;
		if(isFresh)
			++next;
		const auto & expected = computed[index];
		const auto & actual = reused[index];
		if(isFresh ? !sameValue(*expected, *actual) : expected != actual)
			return "record " + std::to_string(index) + " (type " + std::to_string(static_cast<int>(expected->type))
				+ ", value " + std::to_string(expected->val) + ") differs";
	}
	return {};
}

bool environmentFlag(const char * name, std::string_view value)
{
	const char * actual = std::getenv(name);
	return actual && std::string_view(actual) == value;
}

bool checkerEnabled()
{
	static const bool enabled = environmentFlag("NH_AI_ADJACENCY_MEMO_CHECK", "1");
	return enabled;
}

/// NH_AI_ADJACENCY_MEMO=0 computes every answer, for A/B measurement.
bool memoDisabled()
{
	static const bool disabled = environmentFlag("NH_AI_ADJACENCY_MEMO", "0");
	return disabled;
}

[[noreturn]] void mismatch(uint32_t unitId, const std::string & what)
{
	logAi->error("Projected adjacency memo mismatch for unit %d: %s", unitId, what);
	std::abort();
}

/// Checker mode: a reused answer must equal a freshly computed and prepared one.
void verifyReuse(uint32_t unitId, const ProjectedAdjacencyBaseline & reused, ProjectedAdjacencyBaseline computed,
	const ProjectedAdjacencySources * sources)
{
	prepare(computed, sources);
	if(!reused.records != !computed.records)
		mismatch(unitId, "projection presence differs");
	if(reused.veteranCohesionEarned != computed.veteranCohesionEarned)
		mismatch(unitId, "Veteran Cohesion differs");
	if(!computed.records)
		return;
	if(reused.fresh != computed.fresh || reused.freshStacked != computed.freshStacked)
		mismatch(unitId, "fresh record positions differ");
	if(const auto what = listMismatch(*reused.records, *computed.records, computed.fresh); !what.empty())
		mismatch(unitId, "unstacked " + what);
	if(const auto what = listMismatch(*reused.stacked, *computed.stacked, computed.freshStacked); !what.empty())
		mismatch(unitId, "stacked " + what);
}
}

TConstBonusListPtr selectProjectedAdjacencyRecords(const ProjectedAdjacencyBaseline & baseline,
	const CSelector & selector, bool unstacked)
{
	if(!unstacked && !baseline.stacked)
	{
		auto result = std::make_shared<BonusList>(*baseline.records);
		result->stackBonuses();
		result->remove_if([&selector](const Bonus * bonus) { return !selector(bonus); });
		return result;
	}
	const auto & records = unstacked ? *baseline.records : *baseline.stacked;
	const auto & fresh = unstacked ? baseline.fresh : baseline.freshStacked;
	auto result = std::make_shared<BonusList>();
	for(size_t index = 0, next = 0; index < records.size(); ++index)
	{
		const bool isFresh = next < fresh.size() && fresh[next] == index;
		if(isFresh)
			++next;
		if(selector(records[index].get()))
			result->push_back(isFresh ? std::make_shared<Bonus>(*records[index]) : records[index]);
	}
	return result;
}

struct ProjectedAdjacencyMemoSlot::State
{
	struct Entry
	{
		ProjectedAdjacencyBranchFacts branch;
		uint64_t epoch = 0;
		std::weak_ptr<const StackWithBonuses> unit;
		std::vector<UnitFacts> roster;
		ProjectedAdjacencyBaseline value;
	};
	struct Layer
	{
		std::weak_ptr<const StackWithBonuses> unit;
		ProjectedAdjacencyLayerFacts facts;
		const CStack * native = nullptr;
	};
	std::unordered_map<uint32_t, Entry> entries;
	std::unordered_map<const StackWithBonuses *, Layer> layers;
};

ProjectedAdjacencyMemoSlot::ProjectedAdjacencyMemoSlot() = default;
ProjectedAdjacencyMemoSlot::ProjectedAdjacencyMemoSlot(const ProjectedAdjacencyMemoSlot &) noexcept {}
ProjectedAdjacencyMemoSlot & ProjectedAdjacencyMemoSlot::operator=(const ProjectedAdjacencyMemoSlot &) noexcept
{
	state.reset();
	++epoch;
	return *this;
}
ProjectedAdjacencyMemoSlot::~ProjectedAdjacencyMemoSlot() = default;

ProjectedAdjacencyBaseline ProjectedAdjacencyMemoSlot::lookup(const StackWithBonuses & unit,
	const ProjectedAdjacencyBranchFacts & branch, const std::vector<const battle::Unit *> & roster,
	const Compute & compute) const
{
	auto self = unit.weak_from_this();
	if(self.expired() || memoDisabled())
		return compute(); // Without shared ownership the unit has no identity to key.
	auto facts = rosterFacts(roster);
	if(!state)
		state = std::make_unique<State>();
	// No reference into the map survives compute(): a limiter may query another
	// unit of this branch and insert its entry.
	if(const auto found = state->entries.find(unit.unitId()); found != state->entries.end())
	{
		const auto & entry = found->second;
		if(entry.epoch == epoch && sameBranch(entry.branch, branch) && entry.unit.lock().get() == &unit
			&& entry.roster == facts)
		{
			const auto reused = entry.value;
			if(checkerEnabled())
				verifyReuse(unit.unitId(), reused, compute(), branch.sources.get());
			return reused;
		}
	}
	const auto startEpoch = epoch;
	auto value = compute();
	prepare(value, branch.sources.get());
	if(epoch == startEpoch) // A mutation during compute() leaves nothing to reuse.
		state->entries[unit.unitId()] = {branch, epoch, self, std::move(facts), value};
	return value;
}

const CStack * ProjectedAdjacencyMemoSlot::nativeSource(const StackWithBonuses & unit,
	const ProjectedAdjacencyLayerFacts & layer, const std::function<const CStack *()> & compute) const
{
	auto self = unit.weak_from_this();
	if(self.expired() || memoDisabled())
		return compute();
	if(!state)
		state = std::make_unique<State>();
	if(const auto found = state->layers.find(&unit); found != state->layers.end()
		&& found->second.facts == layer && found->second.unit.lock().get() == &unit)
	{
		const auto * native = found->second.native;
		if(checkerEnabled() && compute() != native)
			mismatch(unit.unitId(), "native source differs");
		return native;
	}
	const auto * native = compute();
	state->layers[&unit] = {self, layer, native};
	return native;
}
