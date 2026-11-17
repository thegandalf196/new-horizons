/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../Global.h"
#include "../../lib/battle/CHealth.h"
#include "../../lib/combatScripts/NativeDamageFacts.h"
#include "../../lib/battle/BattleHex.h"
#include "../../lib/battle/BattleSide.h"
#include "../../lib/entities/hero/LeadershipComposition.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include <array>
#include <cstdint>
#include <memory>
#include <limits>
#include <span>
#include <optional>
#include <vector>

namespace bonusDependencies { class CapturedModifiers; class OwnedBonusQueryFrame; struct NumericFrame; }
namespace battle { class UnitStateValueSnapshot; }
namespace nativeRules { class CapturedRuleDocument; class CapturedPerkRules; }
namespace newHorizonsFocusMagic { struct ValueState; }
namespace forecastEffects { class State; }
namespace forecast
{
class NativeReadView;
struct NativeReadDescriptor;
using UnitId = uint32_t;
enum class Event : uint8_t { Damage, Healing, Move, Activation, Retaliation, Strike, Effect, Round, HeroResource, Reaction, Count };
using EventMask = uint32_t;
constexpr EventMask eventBit(Event event)
{
	const auto index = static_cast<unsigned>(event);
	return index < static_cast<unsigned>(Event::Count) ? EventMask{1} << index : 0;
}
enum Dirty : uint32_t { Clean = 0, Health = 1, Damage = 2, Adjacency = 4, Occupancy = 8, Queue = 16, Effects = 32 };
enum class Facing : uint8_t { Left, Right };

struct SourceIdentity
{
	BattleID battle;
	PlayerColor observer;
	int32_t round = 0;
	int32_t activationSerial = 0;
	std::shared_ptr<const nativeRules::CapturedRuleDocument> magicOwner;
	bool operator==(const SourceIdentity &) const = default;
};
/// Closed immutable numerical definition. No Unit, callback, bonus-query cache,
/// mutable bonus payload or source-world address is retained.
struct LeadershipFacts
{
	bool known = false, active = false;
	int64_t effectiveLeadership = 0, requirement = 0, capacity = 1;
	BattleSide controllingSide = BattleSide::NONE;
	std::optional<ObjectInstanceID> hero;
	uint32_t heroLevel = 0;
	int32_t heroTreeVersion = 0;
	std::shared_ptr<const nativeRules::CapturedRuleDocument> capabilityOwner;
	std::shared_ptr<const nativeRules::CapturedPerkRules> perkOwner;
	newHorizonsHeroes::LeadershipComposition composition;
};
struct UnitDefinition
{
	std::shared_ptr<const NativeReadDescriptor> readDescriptor;
	UnitId id;
	CreatureID creature;
	PlayerColor originalOwner;
	BattleSide side;
	int32_t baseAmount = 0;
	uint32_t maximumHealth = 0;
	int32_t movement = 0, initiative = 0;
	bool doubleWide = false;
	EventMask supported = 0;
	LeadershipFacts leadership;
	std::shared_ptr<const bonusDependencies::CapturedModifiers> modifiers;
	std::array<std::shared_ptr<const nativeDamage::PreparedUnit>, 2> numeric;
};
struct Counter
{
	int32_t available = 0, total = 0;
	bool limited = true;
};
struct Activation
{
	bool moved = false, waited = false, waiting = false, defending = false, hadMorale = false;
	int32_t serial = 0;
	uint32_t completed = 0;
	Counter shots, retaliation, casts;
	bool waitBonusUsed = false, waitMasteryDoubled = false;
};
struct UnitDelta
{
	std::shared_ptr<const battle::UnitStateValueSnapshot> nativeValues;
	std::shared_ptr<const bonusDependencies::OwnedBonusQueryFrame> queries;
	std::shared_ptr<const NativeReadView> readView;
	battle::CHealth health; // Detached, owning native ledger; owner is always null.
	std::shared_ptr<const newHorizonsFocusMagic::ValueState> focus;
	std::shared_ptr<const forecastEffects::State> effects;
	BattleHex position;
	Facing facing;
	PlayerColor controller;
	Activation activation;
	int64_t veteranPhysicalDamage = 0;
	int32_t movement = 0, initiative = 0;
	int32_t warfareStat = 0;
	std::shared_ptr<const bonusDependencies::CapturedModifiers> currentModifiers;
	std::shared_ptr<const bonusDependencies::NumericFrame> modifierNumbers;
	std::array<std::shared_ptr<const nativeDamage::PreparedUnit>, 2> numeric;
	explicit UnitDelta(const battle::CHealth & value) : health(value) {}
};
struct UnitInput
{
	std::shared_ptr<const UnitDefinition> definition;
	UnitDelta state;
};
struct Counters
{
	uint64_t rootCaptures = 0, rootAdmissions = 0, sourceFallbacks = 0, lifecycleFallbacks = 0;
	uint64_t referenceOverrides = 0;
	uint64_t forks = 0, committedTransitions = 0, refusedTransitions = 0;
	uint64_t detachedHealthUpdates = 0, neighborhoodInvalidations = 0;
	uint64_t compactFactHits = 0, compactFactMisses = 0, derivedRefreshes = 0;
	uint64_t nativeReadMaterializations = 0, nativeOwningLoans = 0, nativeQueryRebindings = 0;
	bool saturated = false;
	void add(uint64_t Counters::* field, uint64_t amount = 1) noexcept
	{
#ifdef NH_PERF_EXPERIMENTS
		auto & value = (*this).*field;
		const auto room = std::numeric_limits<uint64_t>::max() - value;
		if(amount > room) { value += room; saturated = true; }
		else value += amount;
#else
		(void)field; (void)amount;
#endif
	}
	void accumulate(const Counters & source) noexcept
	{
#ifdef NH_PERF_EXPERIMENTS
		for(auto field : {&Counters::rootCaptures, &Counters::rootAdmissions, &Counters::sourceFallbacks,
			&Counters::lifecycleFallbacks, &Counters::referenceOverrides, &Counters::forks, &Counters::committedTransitions,
			&Counters::refusedTransitions, &Counters::detachedHealthUpdates, &Counters::neighborhoodInvalidations,
			&Counters::compactFactHits, &Counters::compactFactMisses, &Counters::derivedRefreshes, &Counters::nativeReadMaterializations,
			&Counters::nativeOwningLoans, &Counters::nativeQueryRebindings}) add(field, source.*field);
		saturated |= source.saturated;
#else
		(void)source;
#endif
	}
};
class Transitions;
/// Immutable root plus a flat copy-on-write pointer array of changed-unit
/// deltas. Raw facts remain shared; fork isolates only compact native reader
/// observations and lazy receiver slots. Mutations copy a bounded array
/// of handles, never a provider graph or all unit ledgers. Lookup has no parent
/// chain. Sparse public IDs map to contiguous slots, never allocation sizes.
class DLL_LINKAGE State
{
	struct Root;
	struct Layer;
	std::shared_ptr<const Root> root;
	std::shared_ptr<const Layer> layer;
	std::shared_ptr<const std::vector<std::shared_ptr<const NativeReadView>>> readerViews;
	mutable Counters counters;
	int32_t round;
	State(std::shared_ptr<const Root> root, std::shared_ptr<const Layer> layer, Counters counters);
	friend class Transitions;
	friend class NativeAdapter;
	void commit(UnitId id, UnitDelta state, uint32_t dirty, bool neighborhood);
public:
	static State capture(SourceIdentity source, std::vector<UnitInput> units, EventMask planned);
	State fork(std::span<const std::optional<int32_t>> nativeCaches = {}) const;
	bool sameSnapshot(const State & other) const noexcept
	{
		return root == other.root && layer == other.layer && round == other.round;
	}
	std::span<const UnitId> orderedIds() const noexcept;
	const UnitDefinition * definition(UnitId id) const;
	const UnitDelta * unit(UnitId id) const;
	std::shared_ptr<const NativeReadView> readView(UnitId id) const;
	const nativeDamage::PreparedUnit * prepared(UnitId id, bool shooting) const;
	std::span<const UnitId> ids() const;
	const SourceIdentity & source() const;
	bool supports(Event event) const;
	uint64_t revision() const;
	int32_t currentRound() const { return round; }
	uint32_t dirty(UnitId id) const;
	const Counters & work() const { return counters; }
};
}
