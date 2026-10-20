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

class HypotheticBattle;
class CStack;
class InitialExportBattleSubject;
struct MeleeBonusSource;

class StackWithBonuses : public battle::CUnitState, public virtual IBonusBearer,
	public std::enable_shared_from_this<StackWithBonuses>
{
	friend class InitialExportBattleSubject;
public:
	/// Exact original export owner plus uncopied unit-local overlay identities.
	bool captureMeleeBonusSource(MeleeBonusSource & result) const;
	void suppressMeleeBonusIdentities(const std::set<std::shared_ptr<Bonus>> & records);
	// Queries must retain each local record's identity: descendant removals
	// suppress the original pointer, not every bonus with an equal value.
	std::vector<std::shared_ptr<Bonus>> bonusesToAdd;
	std::vector<std::shared_ptr<Bonus>> bonusesToUpdate;
	std::set<std::shared_ptr<Bonus>> bonusesToRemove;
	int treeVersionLocal;

	StackWithBonuses(const HypotheticBattle * Owner, const battle::CUnitState * Stack);

	StackWithBonuses(const HypotheticBattle * Owner, const battle::Unit * Stack);

	StackWithBonuses(const HypotheticBattle * Owner, const battle::UnitInfo & info);

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
	int64_t getRebirthOriginalAggregateHP() const override;

	///IBonusBearer
	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & cachingStr = "") const override;
	TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override;
	TConstBonusListPtr getBonusesBeforeCreatureAbilitySuppression(
		const CSelector & selector, const std::string & cachingStr = "", bool unstacked = false) const override;

	int32_t getTreeVersion() const override;
	bool isBonusCacheable() const override;
	/// Conservative raw-source proof for movement forecasts, not a re-evaluation
	/// of limited bonuses. Caller retains the original immutable world/graph.
	bool hasMovementInvariantBonusSources() const;
	/// Exact canonical branch query route, not an inference from effective lists.
	/// Does not prove preceding reactions or projected typed-hero semantics.
	bool usesCanonicalMovementQuery(const HypotheticBattle & branch) const;
	recipientBonusOverlay::View recipientOverlayView() const;
	bool captureRecipientOverlayLayers(std::vector<recipientBonusOverlay::Layer> & layers,
		const CStack * & native) const;

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
	void applyNoQuarter(int32_t moraleActivationsRemaining, bool appliedByEnemy = false);
	void consumeNoQuarterActivation();
	void clearNoQuarterRoundBlocker();
	void advanceTimedRound();
	void advanceTimedRound(HypotheticLifecycleTiming::EffectWorkTiming * timing);

	void spendMana(ServerCallback * server, const int spellCost) const override;
	std::string getDescription() const override;

private:
	bool hasStableInheritedBonusList() const;
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
	std::optional<std::vector<Bonus>> exactLocalSpellEffects;
	// New hypothetical units own a detached CStack as their creature/army bonus
	// provenance. Descendant projections retain the projected bearer that they
	// wrap so origBearer never points into a destroyed hypothetical battle.
	std::shared_ptr<CStack> ownedBearer;
	std::shared_ptr<const StackWithBonuses> projectedBearer;
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
	/// Immutable exact HP assigned to a first-generation Elemental Rebirth output.
	int64_t rebirthOriginalAggregateHP = 0;
	uint32_t id;
	BattleSide side;
	PlayerColor player;
	SlotID slot;
};
