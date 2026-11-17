/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "ForecastEffectState.h"
#include "../../lib/battle/TimedEffectValueTransitions.h"
namespace battleSearch { class SearchBudget; }
namespace forecastEffects
{
enum class Refusal : uint8_t { None, Unsupported, Invalid, Stale, Exhausted };
struct Change
{
	RecordId id;
	/// Handles retain exact owned before/after payloads through native binding.
	std::shared_ptr<const Bonus> before;
	std::shared_ptr<const Bonus> after; // null = remove
};
struct Receipt
{
	std::shared_ptr<const State> before;
	std::shared_ptr<const State> after;
	uint64_t expectedRevision = 0;
	Dirty dirty = Dirty::None;
	std::vector<Change> changes;
	bool consumedNormalSlot = false;
	bool blockedExtraActivation = false;
	/// Compare immutable handle AND revision before publishing any owner delta.
	DLL_LINKAGE bool matches(const std::shared_ptr<const State> & current) const;
};
struct Result
{
	Refusal refusal = Refusal::None;
	std::optional<Receipt> receipt;
	explicit operator bool() const { return refusal == Refusal::None && receipt.has_value(); }
};
struct Activation
{
	BattleUnitTurnReason reason = BattleUnitTurnReason::TURN_QUEUE;
	/// Exact battleBeginsActivation result, including genuine Second Wind.
	bool beginsActivation = true;
	bool normalSlotForfeit = false;
	bool deferFrozenThaw = false;
	bool alive = true;
	bool ghost = false;
	uint64_t serial = 0;
};
struct BindObservation
{
	RecordId id = 0;
	bool sourceAliveAdjacent = false;
};
enum class Removal : uint8_t { TargetMoved, RootPreservingRelocation, DeathOrResurrection, LegacyBindTrigger, FrozenShatter };
class DLL_LINKAGE Transitions final
{
	static Result prepare(const std::shared_ptr<const State> &, battleSearch::SearchBudget *);
	static Dirty dirty(const Record &);
	static void remove(State &, Receipt &, RecordId);
public:
	/// currentRound must advance exactly once; opening round is 0 -> 1 only.
	static Result prepareRound(const std::shared_ptr<const State> &, bool openingRound,
		int32_t currentRound, battleSearch::SearchBudget * budget = nullptr);
	static Result prepareActivation(const std::shared_ptr<const State> &, Activation,
		battleSearch::SearchBudget * budget = nullptr);
	static Result prepareActivationEnd(const std::shared_ptr<const State> &, uint64_t serial,
		bool alive, bool ghost, battleSearch::SearchBudget * budget = nullptr);
	/// Exact accepted native effect receipt: explicit removals + already prepared
	/// incoming rows. No spell selection, random roll, or duration recalculation.
	static Result prepareApplication(const std::shared_ptr<const State> &, std::span<const RecordId> removed,
		std::span<const RawRecord> incoming, std::optional<int32_t> frozenApplicationRound = std::nullopt,
		battleSearch::SearchBudget * budget = nullptr);
	/// Qualification comes from native accepted-hit preparation, not damage>0.
	static Result prepareFrozenShatter(const std::shared_ptr<const State> &, bool nativeQualified,
		battleSearch::SearchBudget * budget = nullptr);
	static Result prepareStrike(const std::shared_ptr<const State> &, battle::timedEffects::StrikeClock,
		battleSearch::SearchBudget * budget = nullptr);
	/// Legacy Bind needs a complete observation for every parameterized Bind
	/// record, computed from the changed branch's value occupancy/source state.
	static Result prepareRemoval(const std::shared_ptr<const State> &, Removal,
		bool actuallyMoved, std::span<const BindObservation> observations = std::span<const BindObservation>(), battleSearch::SearchBudget * budget = nullptr);
};
}
