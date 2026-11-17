/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "ForecastStateTransitions.h"
#include "ForecastPreparedRound.h"
#include "ForecastSideTransitions.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/battle/FocusFireState.h"
#include <optional>
#include "ForecastEffectTransitions.h"
#include "../../lib/battle/FocusMagicValueState.h"
#include <set>
class Environment;
class CSelector;
struct HypotheticLifecycleTiming;
class HypotheticBattle;
class AttackPossibility;
class StackWithBonuses;
struct HeroOrderState;
struct FocusFireState;
namespace battle { class CUnitState; }
namespace forecast
{
class SideReceipt;
struct NativeReadEnvironment;

enum class LifecyclePhase { Begin, BeginDeferred, CompleteForfeit, CompleteNormal, Finish, Wait };
enum class AcceptedAttackRoute { BestAction, HeroQueue };
enum class AdmissionFailure { None, UnsupportedEvents, Source, UnitLifecycle, DynamicHealth };
struct Admission
{
	std::optional<State> state;
	AdmissionFailure failure = AdmissionFailure::None;
};
/// Borrow the source only during capture/publication. The resulting model owns
/// no callback, unit/provider, bonus cache, RNG or mutable source-world pointer.
/// This first adapter admits ordinary closed native lifecycle; broader event
/// plans are refused BEFORE a compact branch executes, never mid-transition.
class DLL_LINKAGE NativeAdapter
{
	static BattleHex commitSelectedReceipt(HypotheticBattle &, const AttackPossibility &,
		battle::CUnitState &, const battle::CUnitState *, UnitId originalActor);
public:
	static std::shared_ptr<const forecastEffects::State> captureEffects(const StackWithBonuses &, int32_t round);
	static bool effectClockOwnersSupported(HypotheticBattle &);
	static std::unique_ptr<StackWithBonuses> prepareEffectDraft(const StackWithBonuses &, const HypotheticBattle * prospective = nullptr);
	static bool stageEffects(State &, HypotheticBattle &, UnitId, const forecastEffects::Receipt &,
		StackWithBonuses &, std::shared_ptr<const newHorizonsFocusMagic::ValueState> focusAfter = nullptr, bool receiptAlreadyApplied = false, bool roundReset = false);
	static void publishEffects(HypotheticBattle &, State &&, StackWithBonuses &, StackWithBonuses &) noexcept;
	static forecastEffects::Receipt prepareEffectApplication(const State &, HypotheticBattle &, UnitId,
		const std::vector<Bonus> &, const std::vector<Bonus> &, std::optional<int32_t> = std::nullopt);
	static bool effectApplication(HypotheticBattle &, UnitId, const std::vector<Bonus> & removed,
		const std::vector<Bonus> & added, std::optional<int32_t> frozenStamp = std::nullopt);
	static bool effectUpdate(HypotheticBattle &, UnitId, const std::vector<Bonus> &);
	static bool effectRemoval(HypotheticBattle &, UnitId, forecastEffects::Removal, bool acceptedEvent);
	static forecastEffects::Receipt prepareMovementEffects(const State &, HypotheticBattle &, UnitId,
		forecastEffects::Removal route, bool actuallyMoved);
	static forecastEffects::Receipt prepareRoundEffects(const State &, UnitId, bool openingRound,
		int32_t nextRound, battleSearch::SearchBudget * budget = nullptr);
	static bool stageRoundEffects(State &, HypotheticBattle &, UnitId, const forecastEffects::Receipt &, StackWithBonuses &);
	static forecastEffects::Receipt prepareActivationEffects(const State &, HypotheticBattle &, UnitId,
		BattleUnitTurnReason, bool deferred);
	static forecastEffects::Receipt prepareActivationEndEffects(const State &, HypotheticBattle &, UnitId);
	static bool effectStrike(HypotheticBattle &, UnitId, battle::timedEffects::StrikeClock);
	static std::shared_ptr<const battle::UnitStateValueSnapshot> prepareReadValues(const HypotheticBattle &, UnitId);
	static void stagePureReadValue(State &, HypotheticBattle &, UnitId);
	static void stageNativeReadValue(State &, UnitId, const battle::CUnitState &, int warfare);
	static void refreshReadViews(State &, HypotheticBattle &, std::span<const UnitId> affected, bool contextChanged);
	static std::shared_ptr<battle::CUnitState> acquireOwnedState(const HypotheticBattle &, const battle::CUnitState &);
	static void bindRootReadViews(HypotheticBattle &, std::vector<UnitInput> &);
	static std::shared_ptr<const NativeReadEnvironment> captureReadEnvironment(const HypotheticBattle &,
		std::shared_ptr<const NativeReadEnvironment> previous = nullptr);
	static Admission capture(HypotheticBattle & source, EventMask entirePlan);
	static bool leadershipContextCurrent(const HypotheticBattle &);
	static bool admitsOwnedPair(const HypotheticBattle &, const battle::Unit &, const battle::Unit &);
	static bool ordinaryOrdersSupported(const HypotheticBattle &, BattleSide);
	static bool expireEffectSelector(HypotheticBattle &, UnitId, const CSelector &);
	static void expireActivationEffects(HypotheticBattle &, StackWithBonuses &);
	static bool prepareExchange(HypotheticBattle &, Counters * preparation = nullptr);
	static std::shared_ptr<HypotheticBattle> prepareExchangeBranch(const Environment *,
		const std::shared_ptr<HypotheticBattle> &, Counters *);
	static std::optional<BattleHex> selectedReceiptMovement(HypotheticBattle &, const AttackPossibility &, battle::CUnitState &);
	static bool selectedReceiptResources(HypotheticBattle &, const AttackPossibility &, const battle::CUnitState &,
		const std::shared_ptr<StackWithBonuses> &, const std::shared_ptr<StackWithBonuses> &);
	static void validateAcceptedSnapshot(const battle::CUnitState &, const AttackPossibility &);
	static bool applyAcceptedAttack(HypotheticBattle &, const battle::Unit *, const AttackPossibility &, AcceptedAttackRoute);
	static void replayAcceptedAttackNative(HypotheticBattle &, const battle::Unit *, const AttackPossibility &, AcceptedAttackRoute);
	static void forkExchange(const HypotheticBattle &, HypotheticBattle &);
	static State forkOwnedState(const HypotheticBattle &);
	/// False means no compact model was admitted. An admitted invariant failure
	/// throws before publication; it never silently resumes legacy simulation.
	static bool damage(HypotheticBattle &, battle::CUnitState &, int64_t &,
		battle::DamageProvenance, int64_t * postShield);
	static std::optional<battle::HealInfo> heal(HypotheticBattle &, battle::CUnitState &, int64_t &, EHealLevel, EHealPower);
	static bool updateUnit(HypotheticBattle &, UnitId, const JsonNode &, int64_t healthDelta);
	static bool moveUnit(HypotheticBattle &, UnitId, BattleHex);
	static bool afterAttack(HypotheticBattle &, battle::CUnitState &, bool ranged, bool counter, bool physical, bool accepted = true);
	static std::shared_ptr<const newHorizonsFocusMagic::ValueState> captureFocus(const battle::CUnitState &);
	static bool afterFocusAttack(HypotheticBattle &, battle::CUnitState &, bool ranged, bool counter,
		bool physical, bool accepted = true);
	static void importAcceptedFocus(HypotheticBattle &, battle::CUnitState &, const battle::CUnitState &,
		const HypotheticBattle & acceptedWorld);
	static void importFocusReceipt(State &, const battle::CUnitState &, HypotheticBattle &);
	static bool lifecycle(HypotheticBattle &, UnitId, BattleUnitTurnReason, LifecyclePhase, HypotheticLifecycleTiming * timing = nullptr);
	static void completeActivation(HypotheticBattle &, UnitId);
	static void reserveReceipt(HypotheticBattle &);
	static void importReceipt(HypotheticBattle &, const battle::CUnitState &);
	static std::optional<PreparedEffectRound> prepareRound(HypotheticBattle &,
		const BattleSideArray<std::optional<SideReceipt>> &);
	static void commitRound(HypotheticBattle &, std::optional<PreparedEffectRound>);
	static std::optional<State> prepareSideChange(HypotheticBattle &, BattleSide, const SideReceipt &);
	static std::optional<State> prepareOrderChange(HypotheticBattle &, BattleSide,
		std::shared_ptr<const std::vector<HeroOrderState>>);
	static std::optional<State> prepareFocusFireChange(HypotheticBattle &, BattleSide,
		const std::optional<FocusFireState> &);
	static State prepareMembershipGeometry(HypotheticBattle &, State);
	static void refreshOrderDerived(HypotheticBattle & prospective, State & draft);

	/// Publish a prepared compact delta into the SAME isolated legacy unit view
	/// while unmigrated presentation/value consumers still use CUnitState. This
	/// never accepts live CStack and performs no authoritative command mutation.
	static std::optional<MovePermit> prepareMove(const State &, UnitId, BattleHex,
		const HypotheticBattle &, battleSearch::SearchBudget *);
	static bool publish(const State &, UnitId, HypotheticBattle &, StackWithBonuses &);
};
/// Call-local diagnostic sink. The model retains neither the sink nor owner.
class DLL_LINKAGE WorkScope
{
	const HypotheticBattle & branch;
	Counters * sink;
public:
	WorkScope(const HypotheticBattle & branch, Counters * sink) : branch(branch), sink(sink) {}
	~WorkScope();
};
}
