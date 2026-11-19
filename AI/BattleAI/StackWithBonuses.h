/*
 * StackWithBonuses.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "HypotheticBattleTypes.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/IBonusBearer.h"
#include "../../lib/bonuses/RecipientBonusOverlay.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/GraveDustEffectChanges.h"

namespace frozenScoring { class OwnedSource; }
namespace bonusDependencies { struct QueryFrameBinding; class CapturedModifiers; class OwnedBonusQueryFrame; class RecipientLayerSource; }
namespace forecast { class NativeReadView; class NativeAdapter; }
namespace forecastEffects { class State; }
struct PreparedOwnedReadSource;
class HypotheticBattle;
class OwnedStackQuerySource;
class OwnedFrameEffectQueries;
namespace battle { class OwnedUnitBonusFrame; }
class ProjectedAdjacencyBonuses;
struct ProjectedAdjacencyQuery;
class CStack;
class InitialExportBattleSubject;
struct MeleeBonusSource;

class DLL_LINKAGE StackWithBonuses : public battle::CUnitState, public virtual IBonusBearer,
	public std::enable_shared_from_this<StackWithBonuses>
{
	friend class InitialExportBattleSubject;
	friend class HypotheticBattle;
	friend class forecast::NativeAdapter;
	friend class OwnedFrameEffectQueries;
public:
	/// Exact original export owner plus uncopied unit-local overlay identities.
	bool captureMeleeBonusSource(MeleeBonusSource & result) const;
	std::shared_ptr<const bonusDependencies::CapturedModifiers> captureModifierFacts(
		const battle::OwnedUnitBonusFrame * authorizedOwnedFrame) const;
	void suppressMeleeBonusIdentities(const std::set<std::shared_ptr<Bonus>> & records);
	// Queries must retain each local record's identity: descendant removals
	// suppress the original pointer, not every bonus with an equal value.
	std::vector<std::shared_ptr<Bonus>> bonusesToAdd;
	std::vector<std::shared_ptr<Bonus>> bonusesToUpdate;
	std::set<std::shared_ptr<Bonus>> bonusesToRemove;
	int treeVersionLocal;

	StackWithBonuses(const HypotheticBattle *, std::shared_ptr<const forecast::NativeReadView>,
		std::shared_ptr<const bonusDependencies::CapturedModifiers>, std::shared_ptr<const forecastEffects::State>);
	std::unique_ptr<PreparedOwnedReadSource> prepareOwnedReadSource(std::shared_ptr<const forecast::NativeReadView>,
		std::shared_ptr<const bonusDependencies::CapturedModifiers>, std::shared_ptr<const forecastEffects::State>) const;
	std::optional<std::vector<std::shared_ptr<const Bonus>>> ownedLocalOtherEffects() const;
	std::pair<std::shared_ptr<const bonusDependencies::CapturedModifiers>,
		std::shared_ptr<const bonusDependencies::OwnedBonusQueryFrame>> ownedModifierFacts() const;
	void publishOwnedReadSource(PreparedOwnedReadSource &) noexcept;
	StackWithBonuses(const HypotheticBattle * Owner, const battle::CUnitState * Stack);

	StackWithBonuses(const HypotheticBattle * Owner, const battle::Unit * Stack);

	StackWithBonuses(const HypotheticBattle * Owner, const battle::UnitInfo & info);
	/// Pure retained owned recipient; this overload is not native source admission.
	StackWithBonuses(const HypotheticBattle * Owner, std::shared_ptr<battle::OwnedUnitBonusFrame> frame);
	const battle::UnitCreatureResolver * unitCreatureResolver() const override;
	std::shared_ptr<battle::Unit> acquire() const override;
	std::shared_ptr<battle::CUnitState> acquireState() const override;

	virtual ~StackWithBonuses();

	StackWithBonuses & operator= (const battle::CUnitState & other);

	///IUnitInfo
	const CCreature * unitType() const override;

	int32_t unitBaseAmount() const override;

	uint32_t unitId() const override;
	BattleSide unitSide() const override;
	PlayerColor unitOwner() const override;
	SlotID unitSlot() const override;
	int64_t getBattleStartMaximumAggregateHP() const override;

	///IBonusBearer
	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & cachingStr = "") const override;
	TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override;
	TConstBonusListPtr getBonusesBeforeCreatureAbilitySuppression(
		const CSelector & selector, const std::string & cachingStr = "", bool unstacked = false) const override;

	int32_t getTreeVersion() const override;
	bool isBonusCacheable() const override;
	bool mayHaveBonusType(BonusType type) const override;
	/// Conservative raw-source proof for movement forecasts, not a re-evaluation
	/// of limited bonuses. Caller retains the original immutable world/graph.
	bool hasMovementInvariantBonusSources() const;
	/// Exact canonical branch query route, not an inference from effective lists.
	/// Does not prove preceding reactions or projected typed-hero semantics.
	bool usesCanonicalMovementQuery(const HypotheticBattle & branch) const;
	recipientBonusOverlay::View recipientOverlayView() const;
	/// Read-only closed getter/source proof for one immutable expected-Luck pair.
	bool hasCanonicalDamageQuerySources(const HypotheticBattle & branch) const;
	bool captureRecipientOverlayLayers(std::vector<recipientBonusOverlay::Layer> & layers,
		const CStack * & native) const;

	/// Same original bearer/environment; copies VALUES and overlay records, never
	/// wraps this view and reapplies its overlays. No live state is mutated.
	std::unique_ptr<StackWithBonuses> prepareEffectDraft(const HypotheticBattle * prospective = nullptr) const;
	std::shared_ptr<const bonusDependencies::CapturedModifiers> capturePreparedModifierFacts(const StackWithBonuses &) const;
	int preparedModifierWarfare() const;
	bool preparedEffectSourceMatches(const StackWithBonuses & prepared) const noexcept;
	/// Caller completed all validation/queries on the draft before publication.
	/// Keeps this object's identity, owners, environment and cache closures.
	void publishPreparedEffects(StackWithBonuses & prepared) noexcept;
	void addUnitBonus(const std::vector<Bonus> & bonus);
	std::vector<Bonus> localSpellEffects() const;
	void replaceLocalSpellEffectsExact(const std::vector<Bonus> & replacement);
	void updateUnitBonus(const std::vector<Bonus> & bonus);
	void removeUnitBonus(const std::vector<Bonus> & bonus);
	/// Projects an already selected Purify payload onto this detached state.
	bool applyPurifySelection(const std::vector<SpellID> & spellEffectGroups, bool clearPhysicalPoison,
		bool applyPurifyingMandate = false);
	/// Removes exactly the highest-priority current physical affliction from this detached state.
	bool removeFirstPhysicalAffliction();

	void removeUnitBonus(const CSelector & selector);
	void removeUnitBonus(const CSelector & selector, HypotheticLifecycleTiming::EffectWorkTiming * timing);
	void removeGamblerLuckPenalty();
	void removeGamblerLuckPenalty(HypotheticLifecycleTiming::EffectWorkTiming * timing);
	/// Freeze the current bonus inputs for a retained pre-damage scoring snapshot.
	void freezeDamageScoringBonusSnapshot();
	/// Preparation-only; enclosing T2 receipt grants source authority.
	std::shared_ptr<const frozenScoring::OwnedSource> captureFrozenScoringSource(
		bonusDependencies::QueryFrameBinding binding) const;
	bool stageOwnedModifierFacts(std::shared_ptr<const bonusDependencies::CapturedModifiers>,
		std::shared_ptr<const bonusDependencies::OwnedBonusQueryFrame>);
	void applyNoQuarter(int32_t moraleActivationsRemaining, bool appliedByEnemy = false);
	void consumeNoQuarterActivation();
	void clearNoQuarterRoundBlocker();
	void advanceTimedRound();
	void advanceTimedRound(HypotheticLifecycleTiming::EffectWorkTiming * timing);

	void spendMana(ServerCallback * server, const int spellCost) const override;
	std::string getDescription() const override;

private:
	std::shared_ptr<const bonusDependencies::CapturedModifiers> modifierBaseProgram;
	std::shared_ptr<const bonusDependencies::RecipientLayerSource> modifierSourceLease;
	mutable std::shared_ptr<const bonusDependencies::CapturedModifiers> modifierCurrentProgram;
	mutable std::shared_ptr<const bonusDependencies::OwnedBonusQueryFrame> modifierQueryFrame;
	mutable std::optional<int> modifierQueriesVersion;
	recipientBonusOverlay::Layer ownModifierLayer() const;
	std::shared_ptr<const bonusDependencies::OwnedBonusQueryFrame> currentModifierQueries() const;
	friend class ProjectedAdjacencyBonuses;
	TConstBonusListPtr mergeBonusesImpl(const CSelector & selector, const std::string & cachingStr,
		bool unstacked, const ProjectedAdjacencyQuery * adjacency) const;
	bool hasStableInheritedBonusList() const;
	void replacePhysicalAfflictionMarker(Bonus marker);
	void applyOwnedBonusVector(const std::vector<Bonus> & records, newHorizonsGraveDust::EffectOperation operation);
	void removeUnitBonusImpl(const CSelector & selector, bool mayAffectControlState,
		HypotheticLifecycleTiming::EffectWorkTiming * timing);
	TConstBonusListPtr mergeBonuses(const CSelector & selector, const std::string & cachingStr,
		bool unstacked) const;
	void setOriginalBearer(const IBonusBearer * bearer);
	void onBattleFormChanged() override;

	// Value snapshots survive nested models whose bonus queries create fresh pointers.
	// Include spell/command effects and captured affliction groups; only N_TURNS are aged.
	std::optional<std::vector<Bonus>> projectedEffects;
	// Preserve the historical stacked snapshot above while retaining every
	// source bonus and its shared identity for source-sensitive mechanics in
	// getUnstackedBonuses.
	std::optional<std::vector<std::shared_ptr<Bonus>>> projectedUnstackedEffects;
	/// Immutable effective inputs used by branch-local magical-hit valuation.
	std::optional<std::vector<Bonus>> damageScoringBonuses;
	std::optional<std::vector<std::shared_ptr<Bonus>>> damageScoringUnstackedBonuses;
	// Branch-local history: once a marked source/sid group is captured, remember
	// its identity even if the marker is removed or expires. This is only for
	// retaining detached projections; it does not make the group an affliction.
	std::set<std::pair<BonusSource, BonusSourceID>> capturedPhysicalAfflictionGroups;
	void captureEffects();
	void captureEffects(HypotheticLifecycleTiming::EffectWorkTiming * timing);
	void captureLocalSpellEffects();
	std::optional<int> preparedEffectBaseVersion;
	const HypotheticBattle * preparedEffectOriginOwner = nullptr;
	std::optional<std::vector<Bonus>> exactLocalSpellEffects;
	// New hypothetical units own a detached CStack as their creature/army bonus
	// provenance. Descendant projections retain the projected bearer that they
	// wrap so origBearer never points into a destroyed hypothetical battle.
	std::unique_ptr<OwnedStackQuerySource> ownedQuerySource;
	std::shared_ptr<CStack> ownedBearer;
	std::shared_ptr<const StackWithBonuses> projectedBearer;
	std::shared_ptr<const forecast::NativeReadView> ownedReadSource;
	std::shared_ptr<const forecastEffects::State> modifierBaseEffects;
	const IBonusBearer * origBearer;
	/// Typed view of the final bearer binding; bonus values are still queried live.
	const battle::Unit * originalUnit = nullptr;
	const HypotheticBattle * owner;

	// Immutable creature identity used as the native-bonus source when this view
	// reverts. sourceCreatureType is the species already exposed by origBearer;
	// nested projections must suppress it as well as the original species.
	const CCreature * type;
	CreatureID sourceCreatureType;
	ui32 baseAmount;
	/// Frozen source HP basis copied from the authoritative unit for detached forecasts.
	/// A new hypothetical summon has no eligible source basis.
	int64_t battleStartMaximumAggregateHP = 0;
	uint32_t id;
	BattleSide side;
	PlayerColor player;
	SlotID slot;
};
