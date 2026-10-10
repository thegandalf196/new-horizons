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

#include <vstd/RNG.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include <vcmi/Environment.h>
#include <vcmi/ServerCallback.h>

#include "../../lib/bonuses/Bonus.h"
#include "../../lib/battle/BattleProxy.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/HeroActionAllowanceState.h"
#include "../../lib/battle/NewHorizonsElementalRebirth.h"
#include "../../lib/battle/ReducedExtraActivationState.h"
#include "../../lib/battle/SpellResponseState.h"
#include "../../lib/battle/OverwhelmingFormulaState.h"
#include "../../lib/battle/PerfectFortuneState.h"

class HypotheticBattle;
class CSpell;
class CStack;

/// Detached projection result captured before one-strike Luck modifiers expire.
/// UNKNOWN preserves probabilistic damage without inventing a sampled outcome.
enum class ProjectedLuckOutcome : uint8_t
{
	UNKNOWN,
	POSITIVE,
	NEGATIVE,
	NEUTRAL
};

///Fake random generator, used by AI to evaluate random server behavior
class RNGStub final : public vstd::RNG
{
public:
	int nextInt() override
	{
		return 0;
	}

	int nextBinomialInt(int coinsCount, double coinChance) override
	{
		return coinsCount * coinChance;
	}

	int nextInt(int lower, int upper) override
	{
		return (lower + upper) / 2;
	}
	int64_t nextInt64(int64_t lower, int64_t upper) override
	{
		return (lower + upper) / 2;
	}
	double nextDouble(double lower, double upper) override
	{
		return (lower + upper) / 2;
	}

	int nextInt(int upper) override
	{
		return upper / 2;
	}
	int64_t nextInt64(int64_t upper) override
	{
		return upper / 2;
	}
	double nextDouble(double upper) override
	{
		return upper / 2;
	}
};

class StackWithBonuses : public battle::CUnitState, public virtual IBonusBearer,
	public std::enable_shared_from_this<StackWithBonuses>
{
public:
	std::vector<Bonus> bonusesToAdd;
	std::vector<Bonus> bonusesToUpdate;
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
		const CSelector & selector, const std::string & cachingStr = {}, bool unstacked = false) const override;

	int32_t getTreeVersion() const override;

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
	/// Freeze the current bonus inputs for a retained pre-damage scoring snapshot.
	void freezeDamageScoringBonusSnapshot();
	void applyNoQuarter(int32_t moraleActivationsRemaining, bool appliedByEnemy = false);
	void consumeNoQuarterActivation();
	void clearNoQuarterRoundBlocker();
	void advanceTimedRound();

	void spendMana(ServerCallback * server, const int spellCost) const override;
	std::string getDescription() const override;

private:
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
	void captureLocalSpellEffects();
	std::optional<std::vector<Bonus>> exactLocalSpellEffects;
	// New hypothetical units own a detached CStack as their creature/army bonus
	// provenance. Descendant projections retain the projected bearer that they
	// wrap so origBearer never points into a destroyed hypothetical battle.
	std::shared_ptr<CStack> ownedBearer;
	std::shared_ptr<const StackWithBonuses> projectedBearer;
	const IBonusBearer * origBearer;
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

struct ProjectedPrimalBurstHit
{
	std::shared_ptr<StackWithBonuses> preHitTarget;
	PlayerColor targetController;
	int64_t actualDamage = 0;
};

struct ProjectedOverwatchHit
{
	uint32_t shooterId = 0;
	BattleHex position;
	int64_t healthLoss = 0;
};

class HypotheticBattle final : public BattleProxy, public battle::IUnitEnvironment
{
public:
	std::map<uint32_t, std::shared_ptr<StackWithBonuses>> stackStates;

	const Environment * env;

	HypotheticBattle(const Environment * ENV, Subject realBattle);

	bool unitHasAmmoCart(const battle::Unit * unit) const override;
	PlayerColor unitEffectiveOwner(const battle::Unit * unit) const override;
	int unitFortuneSpeed(const battle::Unit * unit) const override { return battleFortuneSpeed(unit); }
	int unitSpeedBonus(const battle::Unit * unit) const override { return battleBloodrageSpeed(unit); }
	int unitBloodragePainIncrement(const battle::Unit * unit) const override;
	bool unitHasVeteranCohesion(const battle::Unit * unit) const override;
	std::optional<int> unitMagicResistance(const battle::Unit * unit) const override;
	std::optional<std::pair<int32_t, int32_t>> unitMoraleLimits(const battle::Unit * unit) const override;
	int unitAdditionalRetaliations(const battle::Unit * unit) const override
	{
		return battleBloodrageRetaliations(unit);
	}

	std::shared_ptr<StackWithBonuses> getForUpdate(uint32_t id);

	BattleID getBattleID() const override;
	ui8 getTacticDist() const override;
	BattleSide getTacticsSide() const override;
	const BattleDeploymentState & getDeploymentState() const override { return deploymentState; }
	void setDeploymentState(const BattleDeploymentState & state) override;
	const ReducedExtraActivationState & getReducedExtraActivationState(BattleSide side) const override;
	void setReducedExtraActivationState(BattleSide side, const ReducedExtraActivationState & state) override;
	const newHorizonsCrossSchoolFormula::State & getCrossSchoolFormulaState(BattleSide side) const override
	{
		static const newHorizonsCrossSchoolFormula::State empty;
		return side == BattleSide::ATTACKER || side == BattleSide::DEFENDER ? crossSchoolFormulaStates.at(side) : empty;
	}
	void setCrossSchoolFormulaState(BattleSide side, const newHorizonsCrossSchoolFormula::State & state) override;
	const SpellResponseState & getSpellResponseState(BattleSide side) const override;
	int32_t getExtendSpellLastRound(BattleSide side) const override { return extendSpellRounds.at(side); }
	void consumeExtendSpell(BattleSide side) override;
	void setSpellResponseState(BattleSide side, const SpellResponseState & state) override;
	const OverwhelmingFormulaState & getOverwhelmingFormulaState(BattleSide side) const override;
	void setOverwhelmingFormulaState(BattleSide side, const OverwhelmingFormulaState & state) override;
	std::vector<HeroOrderState> getHeroOrderStates(BattleSide side) const override;
	std::optional<HeroOrderState> getHeroOrderState(BattleSide side, HeroCommand command) const override;
	std::optional<HeroOrderState> getHeroOrderState(BattleSide side) const override;
	std::vector<HeroOrderState> battleGetHeroOrderStates(BattleSide side) const override;
	std::optional<HeroOrderState> battleGetHeroOrderState(BattleSide side, HeroCommand command) const override;
	std::optional<HeroOrderState> battleGetHeroOrderState(BattleSide side) const override;
	HeroCommand getActiveOrder(BattleSide side) const override;
	const RelentlessAssaultState & battleGetRelentlessAssaultState(BattleSide side) const override;
	const RelentlessAssaultState & getRelentlessAssaultState(BattleSide side) const override;
	void setRelentlessAssaultState(BattleSide side, const RelentlessAssaultState & state) override;
	void recordRelentlessAssaultAttack(BattleSide side, uint32_t targetUnitId) override;
	/// Consume the copied Protect state immediately after hypothetical redirection, before effects resolve.
	bool consumeHeroOrderProtectInterception(uint32_t wardUnitId, uint32_t protectorUnitId);
	const AlternatingHeroActionState & getWarcastingState(BattleSide side) const override;
	const HeroActionAllowanceState & getHeroActionAllowances(BattleSide side) const override;
	const DoubleCommandState & getDoubleCommandState(BattleSide side) const override
	{
		return doubleCommandStates.at(side);
	}
	const PreCombatOrderState & getPreCombatOrderState(BattleSide side) const override
	{
		return preCombatOrderStates.at(side);
	}
	void setPreCombatOrderState(BattleSide side, const PreCombatOrderState & state) override;
	bool hasCompletedHeroSpellCast(BattleSide side) const override { return heroSpellCastCompletedStates.at(side); }
	bool hasCompletedHeroSpellLevel(BattleSide side, int32_t level) const override;
	bool getCounterspellArmed(BattleSide side) const override { return counterspellArmedStates.at(side); }
	int32_t getMetamagicPendingCount(BattleSide side) const override { return metamagicStates.at(side).pending; }
	int32_t getMetamagicUsesConsumed(BattleSide side) const override { return metamagicStates.at(side).uses; }
	bool getMetamagicGrandUsed(BattleSide side) const override { return metamagicStates.at(side).grandUsed; }
	SpellID getMetamagicFirstSpell(BattleSide side) const override { return metamagicStates.at(side).firstSpell; }
	uint32_t getMetamagicFirstTargetUnitId(BattleSide side) const override { return metamagicStates.at(side).firstTarget; }
	const std::vector<SpellID> & getMetamagicSequenceSpells(BattleSide side) const override
	{
		return metamagicStates.at(side).sequence;
	}
	bool getMetamagicFirstCounterspellNegated(BattleSide side) const override
	{
		return metamagicStates.at(side).firstCountered;
	}
	bool getMetamagicCountersequenceArmed(BattleSide side) const override
	{
		return countersequenceArmedStates.at(side);
	}
	struct ProjectedActionReceipt
	{
		BattleSide side = BattleSide::NONE;
		HeroActionAllowanceState::Receipt receipt;
		bool typedLedger = true;
		uint64_t epoch = 0;

		bool operator==(const ProjectedActionReceipt &) const = default;

		bool isHeroAction() const
		{
			return receipt.allowance == HeroActionAllowanceState::AllowanceKind::HERO;
		}
	};
	struct ProjectedMetamagicSnapshot
	{
		uint8_t uses = 0;
		uint8_t pending = 0;
		bool grandUsed = false;
		SpellID firstSpell;
		uint32_t firstTarget = std::numeric_limits<uint32_t>::max();
		bool firstCountered = false;
		std::vector<SpellID> sequence;

		bool operator==(const ProjectedMetamagicSnapshot &) const = default;
	};
	struct ProjectedSpellAllowance
	{
		ProjectedActionReceipt action;
		HeroActionAllowanceState allowancesBefore;
		HeroActionAllowanceState allowancesAfter;
		ProjectedMetamagicSnapshot metamagicBefore;
		SpellID spell;
		bool metamagicFollowup = false;
		bool grand = false;
		uint8_t usesAfter = 0;
		uint8_t pendingAfter = 0;
		bool grandUsedAfter = false;

		bool operator==(const ProjectedSpellAllowance &) const = default;
	};
	struct ProjectedOrderAllowance
	{
		ProjectedActionReceipt action;
		HeroActionAllowanceState allowancesBefore;
		HeroActionAllowanceState allowancesAfter;
		ProjectedMetamagicSnapshot metamagicBefore;

		bool operator==(const ProjectedOrderAllowance &) const = default;
	};
	struct ProjectedCounterspellOutcome
	{
		BattleSide wardSide = BattleSide::NONE;
		bool wardActive = false;
		bool resolutionKnown = false;
		std::optional<bool> negated;
		std::optional<int> manaCost;
	};
	std::optional<ProjectedSpellAllowance> prepareHeroSpellAllowance(BattleSide side, SpellID spell,
		bool metamagicFollowup, bool grand) const;
	std::optional<ProjectedOrderAllowance> prepareHeroOrderAllowance(BattleSide side) const;
	bool beginProjectedHeroAction(BattleSide side, const ProjectedSpellAllowance & prepared);
	bool beginProjectedHeroAction(BattleSide side, const ProjectedOrderAllowance & prepared);
	bool projectAcceptedHeroSpell(BattleSide side, SpellID spell, uint32_t target,
		bool metamagicFollowup, bool grand, bool counterspellWardActive, bool counterspellNegated,
		const ProjectedSpellAllowance & prepared, const std::vector<uint32_t> & affectedRecipients = {});
	bool projectAcceptedHeroOrder(BattleSide side, const ProjectedOrderAllowance & prepared);
	bool projectAcceptedHeroOrder(BattleSide side, HeroCommand command,
		const std::vector<uint32_t> & commandTargets, const ProjectedOrderAllowance & prepared);
	ProjectedCounterspellOutcome resolveProjectedCounterspell(BattleSide casterSide, const CSpell * spell) const;
	bool projectHeroSpellAllowance(BattleSide side, SpellID spell, uint32_t target,
		bool metamagicFollowup, bool grand);
	bool projectHeroOrderAllowance(BattleSide side);
	void expireProjectedTimeStops(BattleSide casterSide);
	ui8 getProjectedPendingTimeStopHeroActionSides() const { return pendingTimeStopHeroActionSides; }
	void setHeroOrderStates(BattleSide side, const std::vector<HeroOrderState> & states) override;
	void setHeroOrderState(BattleSide side, const std::optional<HeroOrderState> & state) override;
	std::optional<FocusFireState> getFocusFireState(BattleSide side) const override;
	void setFocusFireState(BattleSide side, const FocusFireState & state);
	ObstacleCList getAllObstacles() const override;
	bool hasObstacleChanges() const { return obstacleChanges; }
	bool hasWallChanges() const { return wallChanges; }
	EWallState getWallState(EWallPart part) const override;
	int32_t getWallStructuralHP(EWallPart part) const override;
	EGateState getGateState() const override;

	int32_t getActiveStackID() const override;
	int32_t getRound() const override;
	int32_t getBattlecraftMasteryAwardRound(BattleSide side) const override;
	void awardBattlecraftMastery(BattleSide side, uint32_t unitId, int32_t round,
		BattlecraftMasteryAction action) override;
	bool armorerLastStandUsed(BattleSide side) const override;
	ArmorerDefiantState getArmorerDefiantState(BattleSide side) const override;
	const CGHeroInstance * getSideHero(BattleSide side) const override;
	void setArmorerDefiantState(BattleSide side, const ArmorerDefiantState & state) override;
	void consumeArmorerLastStand(BattleSide side) override;
	void applyArmorerLastStandDefend(uint32_t unitId);
	bool getRebirthChainUsed(BattleSide side) const override;
	void setRebirthChainUsed(BattleSide side, bool used) override;
	bool getPhoenixSparkUsed(BattleSide side) const override;
	void setPhoenixSparkUsed(BattleSide side, bool used) override;
	int32_t getBloodrageDamagePercent(BattleSide side) const override;
	int32_t getBloodrageCapPercent(BattleSide side) const override;
	int32_t getBloodrageSpeedBonus(BattleSide side) const override;
	int32_t getBloodrageAdditionalRetaliations(BattleSide side) const override;
	int32_t getBloodrageLowHealthIncrement(BattleSide side) const override;
	int32_t getBloodragePainIncrement(BattleSide side) const override;
	SylvanLuckState getSylvanLuckState(BattleSide side) const override { return fortuneStates.at(side); }
	PerfectFortuneState getPerfectFortuneState(BattleSide side) const override { return perfectFortuneStates.at(side); }
	LuckSerendipityState getLuckSerendipityState(BattleSide side) const override { return luckSerendipityStates.at(side); }
	void setLuckSerendipityState(BattleSide side, const LuckSerendipityState & state) override
	{
		state.validateTransitionFrom(luckSerendipityStates.at(side), getRound());
		luckSerendipityStates.at(side) = state;
	}
	void setPerfectFortuneState(BattleSide side, const PerfectFortuneState & state) override
	{
		state.validate();
		perfectFortuneStates.at(side) = state;
	}
	void setSylvanLuckState(BattleSide side, const SylvanLuckState & state) { fortuneStates.at(side) = state; }
	AdverseCombatRerollState getAdverseCombatRerollState(BattleSide side) const override
	{
		return adverseRerollStates.at(side);
	}
	void setAdverseCombatRerollState(BattleSide side, const AdverseCombatRerollState & state) override
	{
		if(state.used && !state.enabled)
			throw std::runtime_error("Adverse combat reroll expenditure without an enabled perk");
		adverseRerollStates.at(side) = state;
	}
	void endFortuneActivation()
	{
		for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			fortuneStates.at(side).endActivation();
		if(activeUnitId >= 0)
		{
			const auto unitId = static_cast<uint32_t>(activeUnitId);
			if(stackStates.contains(unitId) || subject->battleGetUnitByID(unitId))
				getForUpdate(unitId)->setActivationMovementBonus(0);
		}
	}
	/// A completed forecast action, not WAIT or a same-activation continuation.
	void completeSwiftNormalActivation(uint32_t unitId);
	MoraleSuppressionState getMoraleSuppressionState(BattleSide side) const override
	{
		return moraleSuppressionStates.at(side);
	}
	void setMoraleSuppressionState(BattleSide side, const MoraleSuppressionState & state) override
	{
		state.validate();
		moraleSuppressionStates.at(side) = state;
	}
	/// Bounded expected activation delta: the first available suppression protects one prospective event,
	/// not every stack or every round in the evaluated spell's duration.
	float projectMoraleActivationDelta(const battle::Unit * original, const battle::Unit * projected,
		float before, float after, float horizon);
	LuckRollRules getLuckRollRules() const override { return fortuneRollRules; }

	/// Apply Luck history in an isolated or selected branch, never live state.
	/// Record-only mode expires one-strike modifiers before subsequent blows;
	/// post-hit aftermath is applied separately once using the captured outcome.
	bool fortuneStrikeIsCertain(const BattleAttackInfo & attack) const;
	ProjectedLuckOutcome captureFortuneStrikeOutcome(const BattleAttackInfo & attack) const;
	/// Deterministic post-hit cleansing only; stochastic Freezing Touch is valued,
	/// never materialized as a guaranteed marker in a forecast.
	void projectFrozenShatter(const BattleAttackInfo & attack,
		const std::vector<std::pair<uint32_t, int64_t>> & hits);
	void projectFortuneStrike(const BattleAttackInfo & attack,
		const std::vector<std::pair<uint32_t, int64_t>> & hits,
		battle::CUnitState * attackerState, bool enemyStackKilled,
		std::optional<ProjectedLuckOutcome> resolvedLuck = std::nullopt, bool applyAftermath = true,
		std::optional<bool> capturedPerfectFortune = std::nullopt,
		BattleSide capturedPerfectFortuneSide = BattleSide::NONE,
		BattleSide capturedLuckSerendipitySide = BattleSide::NONE,
		std::optional<bool> capturedLuckSerendipityOrdinaryAttack = std::nullopt);
	/// Project only New Horizons Hex of Pain's registered AFTER_ATTACK trigger.
	/// Other COMBAT_EVENT_TRIGGER effects are intentionally outside this model.
	int64_t projectHexOfPainStrike(const BattleAttackInfo & attack,
		const std::vector<std::pair<uint32_t, int64_t>> & hits, int32_t attackIndex = 0);

	battle::Units getUnitsIf(const battle::UnitFilter & predicate) const override;

	void nextRound() override;
	void nextTurn(uint32_t unitId, BattleUnitTurnReason reason) override;

	void addUnit(uint32_t id, const JsonNode & data) override;
	void updateUnit(uint32_t id, const JsonNode & data, int64_t healthDelta) override;
	void moveUnit(uint32_t id, const BattleHex & destination) override;
	/// Explicit ordinary movement only: spell/forced moveUnit callers never react.
	std::vector<ProjectedOverwatchHit> projectVoluntaryMovement(uint32_t id, const BattleHex & destination);
	void removeUnit(uint32_t id) override;
	void recordBloodrageTransition(const std::shared_ptr<StackWithBonuses> & unit, bool wasAlive);

	void addUnitBonus(uint32_t id, const std::vector<Bonus> & bonus) override;
	battle::BattleEffectSnapshot captureBattleEffects(uint32_t id) const override;
	void exchangeBattleEffects(const battle::BattleEffectExchange & exchange) override;
	void updateUnitBonus(uint32_t id, const std::vector<Bonus> & bonus) override;
	void removeUnitBonus(uint32_t id, const std::vector<Bonus> & bonus) override;

	void setWallState(EWallPart partOfWall, EWallState state) override;
	void setWallStructuralHP(EWallPart partOfWall, int32_t hp) override;

	void addObstacle(const ObstacleChanges & changes) override;
	void updateObstacle(const ObstacleChanges& changes) override;
	void removeObstacle(uint32_t id) override;

	uint32_t nextUnitId() const override;
	std::optional<newHorizonsElementalRebirth::DeathSnapshot> captureElementalRebirthSource(
		const battle::Unit & unit) const;
	bool hasReadyNativeRebirth(const battle::Unit * unit) const;
	std::optional<uint32_t> projectElementalRebirth(const battle::Unit * postHitUnit,
		const newHorizonsElementalRebirth::DeathSnapshot & snapshot, bool hitKilled,
		bool cloneKilled, bool nativeRebirth);
	const std::set<uint32_t> & getElementalRebirthSpawnUnitIds() const { return elementalRebirthSpawnUnitIds; }
	bool isElementalRebirthSpawn(uint32_t unitId) const { return elementalRebirthSpawnUnitIds.contains(unitId); }
	const std::vector<ProjectedPrimalBurstHit> & getProjectedPrimalBurstHits() const
	{
		return projectedPrimalBurstHits;
	}

	int64_t getActualDamage(const DamageRange & damage, int32_t attackerCount, vstd::RNG & rng) const override;
	std::vector<SpellID> getUsedSpells(BattleSide side) const override;
	int3 getLocation() const override;
	BattleLayout getLayout() const override;

	int32_t getTreeVersion() const;

	void makeWait(const battle::Unit * activeStack);

	void resetActiveUnit()
	{
		activeUnitId = -1;
	}

	/// Apply only the deterministic ranged-mark reaction after projected hit
	/// damage. This does not dispatch unrelated combat scripts or real packets.
	void projectRangedMarkStrike(const BattleAttackInfo & attack,
		const std::vector<std::pair<uint32_t, int64_t>> & hits);

	ServerCallback * getServerCallback();
	const scripting::Pool & getScriptContextPool() const override;

private:
	/// IDs created by this branch's Elemental Rebirth projection; never serialized.
	std::set<uint32_t> elementalRebirthSpawnUnitIds;
	/// Actual magical HP losses from the accepted projected Primal Burst batches.
	/// Pre-hit snapshots are owned so branch copies don't borrow mutable/live units.
	std::vector<ProjectedPrimalBurstHit> projectedPrimalBurstHits;
	BattleDeploymentState deploymentState;
	BattleSideArray<ReducedExtraActivationState> reducedExtraActivationStates;
	/// Newest Order is last; updates by command preserve this issuance order.
	BattleSideArray<std::vector<HeroOrderState>> heroOrderStates;
	BattleSideArray<AlternatingHeroActionState> warcastingStates;
	BattleSideArray<HeroActionAllowanceState> heroActionAllowances;
	BattleSideArray<DoubleCommandState> doubleCommandStates;
	BattleSideArray<PreCombatOrderState> preCombatOrderStates;
	BattleSideArray<bool> heroSpellCastCompletedStates;
	/// Branch-local per-side round stamp for Battlefield Mastery's first Wait/Defend award.
	BattleSideArray<int32_t> battlecraftMasteryAwardRounds;
	/// Branch-local once-per-combat consumption for Armorer's first qualifying Last Stand.
	BattleSideArray<bool> armorerLastStandUsedStates;
	BattleSideArray<bool> rebirthChainUsedStates;
	BattleSideArray<bool> phoenixSparkUsedStates;
	BattleSideArray<ArmorerDefiantState> armorerDefiantStates;
	BattleSideArray<std::uint8_t> completedHeroSpellLevelMasks;
	BattleSideArray<bool> counterspellArmedStates;
	BattleSideArray<bool> countersequenceArmedStates;
	ui8 pendingTimeStopHeroActionSides = 0;
	using ProjectedMetamagic = ProjectedMetamagicSnapshot;
	BattleSideArray<ProjectedMetamagic> metamagicStates;
	BattleSideArray<uint64_t> projectedActionEpochs{};
	BattleSideArray<std::optional<ProjectedSpellAllowance>> begunProjectedSpellAllowances{};
	BattleSideArray<std::optional<ProjectedOrderAllowance>> begunProjectedOrderAllowances{};
	bool isCurrentPreparedSpellAction(BattleSide side, const ProjectedSpellAllowance & prepared,
		bool requireBegun) const;
	bool isCurrentPreparedOrderAction(BattleSide side, const ProjectedOrderAllowance & prepared,
		bool requireBegun) const;
	void beginProjectedHeroAction(BattleSide side, const ProjectedActionReceipt & receipt);
	void finishProjectedHeroAction(BattleSide side, const ProjectedSpellAllowance & prepared);
	void finishProjectedHeroAction(BattleSide side, const ProjectedOrderAllowance & prepared);
	std::map<BattleSide, std::optional<FocusFireState>> focusFireStates;
	BattleSideArray<RelentlessAssaultState> relentlessAssaultStates;
	BattleSideArray<newHorizonsCrossSchoolFormula::State> crossSchoolFormulaStates;
	BattleSideArray<SpellResponseState> spellResponseStates;
	BattleSideArray<int32_t> extendSpellRounds;
	BattleSideArray<OverwhelmingFormulaState> overwhelmingFormulaStates;
	BattleSideArray<int32_t> bloodrageRanks;
	BattleSideArray<int32_t> bloodrageDamagePercents;
	BattleSideArray<int32_t> bloodrageCaps;
	BattleSideArray<int32_t> bloodrageSpeedBonuses;
	BattleSideArray<int32_t> bloodrageAdditionalRetaliations;
	BattleSideArray<int32_t> bloodrageLowHealthIncrements;
	BattleSideArray<int32_t> bloodragePainIncrements;
	std::set<uint32_t> bloodrageDestroyedUnits;
	bool bloodrageFirstBloodUsed = false;
	BattleSideArray<SylvanLuckState> fortuneStates;
	BattleSideArray<PerfectFortuneState> perfectFortuneStates;
	BattleSideArray<LuckSerendipityState> luckSerendipityStates;
	BattleSideArray<AdverseCombatRerollState> adverseRerollStates;
	BattleSideArray<MoraleSuppressionState> moraleSuppressionStates;
	LuckRollRules fortuneRollRules;

	class HypotheticServerCallback : public ServerCallback
	{
	public:
		HypotheticServerCallback(HypotheticBattle * owner_);

		void complain(const std::string & problem) override;
		bool describeChanges() const override;
		void recordCrossSchoolFormulaCast(BattleSide side, const newHorizonsCrossSchoolFormula::Receipt & receipt) override;
		void recordCompletedHeroSpellCast(BattleSide side) override;
		void recordExtendSpellCast(BattleSide side) override { owner->consumeExtendSpell(side); }
		void recordCompletedHeroSpellCast(BattleSide side, int32_t spellLevel) override;

		vstd::RNG * getRNG() override;
		bool rollCombatAbility(const IBattleInfoCallback & battle, const battle::Unit & actor, int percentageChance) override;
		bool rollHostileCombatAbility(const IBattleInfoCallback & battle, const battle::Unit & actor,
			const battle::Unit & recipient, int percentageChance) override;
		bool resolveAdverseCombatRoll(const BattleID & battleID, BattleSide side,
			bool stochastic, bool adverseOnTrue, const std::function<bool()> & draw) override;

		void apply(CPackForClient & pack) override;

		void apply(BattleLogMessage & pack) override;
		void apply(BattleStackMoved & pack) override;
		void apply(BattleUnitsChanged & pack) override;
		void apply(SetStackEffect & pack) override;
		void apply(StacksInjured & pack) override;
		void apply(BattleObstaclesChanged & pack) override;
		void apply(CatapultAttack & pack) override;
	private:
		HypotheticBattle * owner;
		RNGStub rngStub;
	};

	class HypotheticEnvironment : public Environment
	{
	public:
		HypotheticEnvironment(HypotheticBattle * owner_, const Environment * upperEnvironment);

		const Services * services() const override;
		const BattleCb * battle(const BattleID & battleID) const override;
		const GameCb * game() const override;

	private:
		HypotheticBattle * owner;
		const Environment * env;
	};

	int32_t bonusTreeVersion;
	int32_t activeUnitId;
	int32_t projectedRound;
	ObstacleCList projectedObstacles;
	bool obstacleChanges = false;
	std::map<EWallPart, EWallState> projectedWalls;
	std::map<EWallPart, int32_t> projectedStructuralHP;
	bool canonicalStructuralHP = false;
	EGateState initialGateState = EGateState::NONE;
	bool wallChanges = false;
	mutable uint32_t nextId;

	std::unique_ptr<HypotheticServerCallback> serverCallback;
	std::unique_ptr<HypotheticEnvironment> localEnvironment;
};
