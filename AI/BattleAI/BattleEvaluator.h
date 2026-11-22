/*
 * BattleEvaluator.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once
#include "SearchBudget.h"
#include "../../lib/battle/ReachabilityInfo.h"
#include "PossibleSpellcast.h"
#include "PotentialTargets.h"
#include "BattleExchangeVariant.h"
#include "VinesCandidateValueReuse.h"
#include "ArrowContinuationReuse.h"
#include "LifeDrainCandidateValueReuse.h"
#include "RegenerationBaselineReuse.h"
#include <utility>
#include "HypotheticBattle.h"
#include "HeroSpellOutcomeValue.h"

namespace battleAIDiagnostic { class Admission; }
namespace creatureSpellTiming { struct Sample; struct Pressure; }

class CSpell;
class CBattleCallback;
class BattleAction;
class JsonNode;
class CPlayerBattleCallback;
class CGHeroInstance;
struct DivineMandateStatus;
namespace heroActionTiming { struct Summary; }
namespace heroActionSelection { class Observer; }
namespace heroDecisionPressure { struct Experiment; }

struct CachedAttack
{
	std::optional<AttackPossibility> ap;
	float score = EvaluationResult::INEFFECTIVE_SCORE;
	uint8_t turn = 255;
	bool waited = false;
};

class DLL_LINKAGE BattleEvaluator
{
	std::optional<battleSearch::SearchLimits> workLimits;
	std::shared_ptr<battleSearch::SearchBudget> workBudget;
	std::shared_ptr<MovementQueryCache> movementMemo;
	HypotheticBattle::Subject decisionNativeSource;
	std::shared_ptr<HypotheticBattle> decisionBase;
	forecast::Counters decisionPreparation;
	std::shared_ptr<forecast::NativeReadWork> decisionReadWork;
	bool decisionBaseOpen = true;
	std::shared_ptr<HypotheticBattle> createHypotheticBattle(const HypotheticBattle::Subject & source,
		std::shared_ptr<battleSearch::SearchBudget> explicitWork = {}) const;
	std::shared_ptr<HypotheticBattle> createHeroCandidateBattle(const HypotheticBattle::Subject &,
		const PossibleSpellcast &, std::shared_ptr<battleSearch::SearchBudget> = {}) const;
	std::shared_ptr<HypotheticBattle> createCreatureCandidateBattle(const CStack *, const CSpell *,
		const spells::Target &, std::optional<int> spellLevel = {}) const;
	std::unique_ptr<PotentialTargets> targets;
	std::shared_ptr<HypotheticBattle> hb;
	BattleExchangeEvaluator scoreEvaluator;
	std::shared_ptr<CBattleCallback> cb;
	std::shared_ptr<Environment> env;
	bool activeActionMade = false;
	bool heroSubmissionBegun = false;
	/// Closes the decision base immediately before a Hero Action is sent.
	void beginHeroSubmission() { decisionBaseOpen = false; heroSubmissionBegun = true; }
	BattleAction legalSearchIncumbent(const CStack * stack) const;
	bool submitHeroAction(const PossibleSpellcast & castToPerform, heroActionTiming::Summary * timing);
	bool finishHeroSelection(std::vector<PossibleSpellcast> &, float, bool,
		heroActionSelection::Observer *, heroActionTiming::Summary *);
	CachedAttack cachedAttack;
	void includeSyntheticInspiration(PossibleSpellcast &, const std::shared_ptr<HypotheticBattle> &, uint32_t actorId);
	float conditionalInspirationUtility(const std::shared_ptr<HypotheticBattle> &, uint32_t actorId, bool full);
	float inspirationOutcome(const PossibleSpellcast &, const spells::Mechanics &,
		const std::shared_ptr<HypotheticBattle> &, std::span<const BattleStackAttacked>, bool completed, bool full, uint32_t actorId);
	heroSpellProjection::OutcomeValue inspirationOutcomeEvaluator(const PossibleSpellcast &, const spells::Mechanics &, uint32_t actorId);
	bool prepareBlinkOutcome(PossibleSpellcast &, spells::Mechanics &, uint32_t actorId);
	bool prepareExpectedOutcomes(PossibleSpellcast &, spells::Mechanics &, uint32_t actorId);
	bool prepareOverflowOutcome(PossibleSpellcast &, spells::Mechanics &, uint32_t actorId);

	PlayerColor playerID;
	BattleID battleID;
	BattleSide side;
	DamageCache damageCache;
	float strengthRatio;
	int simulationTurnsCount;
	struct SpellOption
	{
		const CSpell * spell = nullptr;
		bool divineMandateFollowup = false;
	};
	std::vector<SpellOption> collectCastableHeroSpellOptions(
		const std::shared_ptr<CPlayerBattleCallback> & battleView, const CGHeroInstance * hero,
		bool allowSpells, bool mandatoryOrder,
		const DivineMandateStatus & divineMandateStatus,
		const battleAIDiagnostic::Admission * admission) const;
	bool evaluateHeroActionQueue(PossibleSpellcast::ValueMap & values,
		const std::vector<battle::Units> & queue, std::shared_ptr<HypotheticBattle> state,
		size_t minTurnSpan, bool * enemyHadTurnOut);
	void projectHeroActionQueueActivation(PossibleSpellcast::ValueMap & values,
		const std::shared_ptr<HypotheticBattle> & state, const battle::Unit * unit,
		battle::Units & remainingQueue, size_t queueIndex);
	float beneficialCreaturePressure(const std::shared_ptr<HypotheticBattle> & state,
		uint32_t spentCasterId, DamageCache & cache, creatureSpellTiming::Pressure * timing = nullptr) const;
	float beneficialCreatureOutcomeValue(const CStack * caster, const battle::Unit * recipient,
		const CSpell * spell, int spellLevel, float baselinePressure, creatureSpellTiming::Sample * timing = nullptr) const;
	std::optional<PossibleSpellcast> findBestRandomCreatureSpell(const CStack * stack,
		int abilityLevel, creatureSpellTiming::Sample * timing);
	/// Leaves cast-only admission/value in ps; returns HP and future-DPS completion values.
	std::pair<float, float> evaluateCreatureCastBeforeCompletion(const CStack * stack, PossibleSpellcast & ps);
	bool applyCreatureCastProjection(HypotheticBattle & state, const CStack * stack, PossibleSpellcast & ps);

public:
	forecast::Counters takeForecastPreparation() noexcept
	{ auto result = decisionPreparation; decisionPreparation = {}; return result; }
	/// Effective chosen-spell proficiency, matching authoritative creature-cast resolution.
	static int creatureAbilitySpellLevel(const CStack * caster, const CSpell * spell, int abilityLevel);
	/// Optional call-local wall diagnostics. Not gameplay state and never serialized.
	struct DLL_LINKAGE SelectionTiming
	{
		struct DLL_LINKAGE StageTiming
		{
			uint64_t calls = 0;
			uint64_t totalMicroseconds = 0;
		};
		StageTiming creatureSpell;
		StageTiming movementSearch;
		BattleMovementTiming movementDetails;
		/// Counts measured call sites, not the number of candidate exchanges.
		StageTiming targetExchange;
		/// Disjoint call-site partitions sharing the same elapsed sample as targetExchange.
		StageTiming bestTarget;
		BattleBestTargetTiming bestTargetDetails;
		StageTiming siegeDefense;
		StageTiming defensiveStance;
	};

private:
	BattleAction evaluatedAttackAction(const CStack *, const EvaluationResult &, float, bool, SelectionTiming *);
public:
	bool usesFiniteSearchWork() const { return workBudget != nullptr; }
	battleSearch::WorkAmounts searchWorkSpent() const { return workBudget ? workBudget->spent() : battleSearch::WorkAmounts{}; }
	std::array<uint64_t, battleSearch::workKindCount> searchWorkRefusals() const
	{
		return workBudget ? workBudget->refusals() : std::array<uint64_t, battleSearch::workKindCount>{};
	}

	BattleAction selectStackAction(const CStack * stack, SelectionTiming * timing = nullptr);
	/// New captured Gating only; invoked after ordinary Hero/typed decisions.
	std::optional<BattleAction> chooseTemporaryGate(const CStack *, const BattleAction & ordinary);
	/// Optional call-local experiment; null retains the original candidate set.
	bool attemptCastingSpell(const CStack * stack, bool allowSpells = true,
		heroActionSelection::Observer * observer = nullptr,
		VinesCandidateValueReuse::TestControl * vinesReuse = nullptr,
		LifeDrainCandidateValueReuse::TestControl * lifeDrainReuse = nullptr,
		RegenerationBaselineReuse::TestControl * regenerationReuse = nullptr,
		heroDecisionPressure::Experiment * orderExperiment = nullptr,
		arrowContinuation::Reuse::TestControl * arrowReuse = nullptr,
		const battleAIDiagnostic::Admission * admission = nullptr);
	bool canCastSpell();
	/// Whether this evaluator began submitting a Hero Action to the authority.
	bool heroSubmissionStarted() const { return heroSubmissionBegun; }
	std::optional<PossibleSpellcast> findBestCreatureSpell(const CStack * stack, creatureSpellTiming::Sample * timing = nullptr);
	/// Deterministic full-pool forecast; does not choose the eventual random spell.
	float expectedBeneficialCreatureSpellValue(const CStack * caster, const battle::Unit * recipient) const;
	/// One detached outcome in the same forecast scale, exposed for deterministic parity tests.
	float beneficialCreatureSpellOutcomeValue(const CStack * caster, const battle::Unit * recipient,
		const CSpell * spell) const;
	BattleAction goTowardsNearest(const CStack * stack, const BattleHexArray & hexes, const PotentialTargets & targets,
		BattleMovementTiming * timing = nullptr, std::pair<float, float> * movementCompletionValue = nullptr);
	std::vector<BattleHex> getBrokenWallMoatHexes() const;
	bool hasWorkingTowers() const;
	void evaluateCreatureSpellcast(const CStack * stack, PossibleSpellcast & ps); //for offensive damaging spells only
	/// Shared projected value for one Sorrow target; public for deterministic AI-focused tests.
	static float estimateProjectedSorrowTargetValue(const battle::Unit * original, const battle::Unit * projected,
		DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & projectedBattle);
	/// Shared projected value for one Curse target; public for deterministic AI-focused tests.
	static float estimateProjectedCurseTargetValue(const battle::Unit * original, const battle::Unit * projected,
		DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & projectedBattle);
	/// Shared projected value for Frailty's bounded physical-damage increase on one target.
	static float estimateProjectedFrailtyTargetValue(const battle::Unit * original, const battle::Unit * projected,
		DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & projectedBattle,
		DamageCache * explicitBefore = nullptr, bool effectiveAllegiance = false);
	/// Shared projected value for one Hex of Pain target's reduced future attack value.
	static float estimateProjectedHexOfPainTargetValue(const battle::Unit * original,
		const battle::Unit * projected, const std::shared_ptr<HypotheticBattle> & projectedBattle);
	/// Shared projected value for Doom's reduced future attacks and Morale activations.
	static float estimateProjectedDoomTargetValue(const battle::Unit * original,
		const battle::Unit * projected, DamageCache & damageCache,
		const std::shared_ptr<HypotheticBattle> & projectedBattle);
	/// Saved-profile candidate gate shared by Doom discovery and focused AI tests.
	static bool canonicalDoomAvailableInSavedRules(const JsonNode & magicRules, const CSpell * spell);
	void print(const std::string & text) const;
	BattleAction moveOrAttack(const CStack * stack, const BattleHex & hex, const PotentialTargets & targets,
		std::pair<float, float> * movementCompletionValue = nullptr);
	struct DLL_LINKAGE MovementCompletionProjection
	{
		std::shared_ptr<HypotheticBattle> battle;
		std::pair<float, float> value{};
		bool arrivedAlive = false;
	};
	/// Explicit ordinary MOVE only; clones the parent and scores its real completion receipts.
	static MovementCompletionProjection projectOrdinaryMoveCompletion(const Environment * environment,
		const std::shared_ptr<HypotheticBattle> & parent, const BattleAction & action, PlayerColor perspective);

	BattleEvaluator(
		std::shared_ptr<Environment> env,
		std::shared_ptr<CBattleCallback> cb,
		const battle::Unit * activeStack,
		PlayerColor playerID,
		BattleID battleID,
		BattleSide side,
		float strengthRatio,
		int simulationTurnsCount,
		std::shared_ptr<MovementQueryCache> movementCache = nullptr
#ifdef NH_PERF_EXPERIMENTS
		, std::shared_ptr<battleSearch::SearchBudget> diagnosticRoot = nullptr,
		std::shared_ptr<forecast::NativeReadWork> diagnosticReads = nullptr
#endif
	);

	BattleEvaluator(
		std::shared_ptr<Environment> env,
		std::shared_ptr<CBattleCallback> cb,
		std::shared_ptr<HypotheticBattle> hb,
		DamageCache & damageCache,
		const battle::Unit * activeStack,
		PlayerColor playerID,
		BattleID battleID,
		BattleSide side,
		float strengthRatio,
		int simulationTurnsCount);
};
