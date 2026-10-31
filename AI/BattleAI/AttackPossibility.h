/*
 * AttackPossibility.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once
#include "../../lib/battle/NewHorizonsOrdinaryStrike.h"
#include <algorithm>
#include <set>
#include <utility>
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/DamageCalculationTiming.h"
#include "StackWithBonuses.h"
#include "../../lib/battle/NewHorizonsAerialSupremacy.h"
#include "DreadCavalryProjection.h"
#include "PetrifyingBloodValue.h"
#include "ParalyzingGazeProjection.h"
#include "HypotheticBattle.h"
#include "HypotheticBattleTypes.h"
#include "SylvanTacticsProjection.h"

#define BATTLE_TRACE_LEVEL 0

class DamageCacheAdmission;

/// Match BattleActionProcessor's physical-creature provenance rule for a
/// projected attack. Physical hits from turrets and siege/war-machine stacks
/// are not eligible for mechanics which track ordinary creature damage.
inline battle::DamageProvenance battleAIDamageProvenance(const battle::Unit * attacker, bool physicalDamage)
{
	if(!physicalDamage)
		return battle::DamageProvenance::SPELL;
	if(!attacker || attacker->isTurret() || attacker->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| attacker->unitSlot() == SlotID::WAR_MACHINES_SLOT)
		return battle::DamageProvenance::OTHER;
	return battle::DamageProvenance::PHYSICAL_CREATURE;
}

struct BattleAIDamageProjection
{
	/// Damage amount remaining after Guardian Spirit and the stack's health cap.
	/// This is the value exposed to projected post-hit combat effects.
	int64_t appliedDamage = 0;
	/// Health value removed by the hit for attack scoring. A clone loses its
	/// entire health pool when any post-Guardian damage reaches it.
	int64_t healthLoss = 0;
};

inline BattleAIDamageProjection battleAIProjectDamage(const battle::Unit * target,
	int64_t incomingDamage, battle::DamageProvenance provenance)
{
	if(!target || incomingDamage <= 0 || target->isTimeStopped())
		return {};

	if(provenance == battle::DamageProvenance::PHYSICAL_CREATURE
		&& target->getGuardianSpiritRoundsRemaining() > 0)
		incomingDamage -= std::min(incomingDamage, target->getGuardianSpiritHitPoints());

	if(incomingDamage <= 0)
		return {};
	if(target->isClone())
		return {0, target->getAvailableHealth()};

	const auto appliedDamage = std::min(incomingDamage, target->getAvailableHealth());
	return {appliedDamage, appliedDamage};
}

/// Accepted branch-local exemption, not a persistent ignore-retaliation flag.
struct DefiantDenialProjection
{
	enum class Phase : uint8_t { BEFORE_HITS, AFTER_TARGET_HIT };
	BattleSide side = BattleSide::NONE;
	newHorizonsArmorer::DefiantDenialCause cause = newHorizonsArmorer::DefiantDenialCause::INNATE_BLOCK;
	uint32_t targetId = 0;
	int32_t round = -1;
	Phase phase = Phase::AFTER_TARGET_HIT;
};

/// One physical attack (or its retaliation) in the read-only attack preview.
/// Keeping these deltas lets a committed preview replay Fortune aftermath at
/// the same boundaries as the authoritative action instead of collapsing a
/// multi-strike exchange into one post-hoc transition.
struct FortuneStrikeProjection
{
	/// Prehit source facts; replay after this accepted primary, before retaliation.
	newHorizonsOrdinaryStrike::EffectPlan lanceFormation;
	/// nullopt retains legacy inference; a captured UNKNOWN must stay unknown
	/// when a consumed Gambler bonus changes the next strike's Luck value.
	std::optional<ProjectedLuckOutcome> resolvedLuck;
	/// Capture before damage: death or post-hit status changes cannot undo an accepted use.
	bool perfectFortune = false;
	BattleSide perfectFortuneSide = BattleSide::NONE;
	/// Accepted controller/attack classification, captured before hit effects.
	BattleSide luckSerendipitySide = BattleSide::NONE;
	bool luckSerendipityOrdinaryAttack = false;
	uint32_t attackerId = 0;
	uint32_t defenderId = 0;
	/// Explicit primary-shot admission; counter flag/coefficient do not identify reaction families.
	bool paralyzingGazeAdmitted = false;
	std::optional<paralyzingGazeAI::Consumption> gazeConsumption;
	/// Only a typed aerial counter replays these otherwise unrepresented source effects.
	int64_t aerialReflectionDamage = 0;
	int64_t aerialVampirismHealing = 0;
	std::optional<newHorizonsAerialSupremacy::Excursion> aerialAdmission;
	std::optional<newHorizonsAerialSupremacy::Excursion> aerialExcursion;
	int dreadCavalryDamagePercent = 100;
	bool shooting = false;
	bool retaliation = false;
	/// Brace is reactive for Last Stand, but not damage/ordinary retaliation.
	bool bracePreemptive = false;
	int preemptiveDamagePercent = 0; // Retain exclusion during selected replay.
	/// Captures the actual attack-classification decision for Last Stand; damage
	/// provenance alone is broader and can include non-attack physical sources.
	bool eligibleForLastStand = false;
	battle::DamageProvenance damageProvenance = battle::DamageProvenance::OTHER;
	bool perfectMoment = false;
	bool protectIntercepted = false;
	bool relentlessAssaultEligible = false;
	int32_t attackIndex = 0;
	int cleaveDamagePercent = 0;
	/// Damage requests are preserved before shields/health caps for faithful replay.
	std::vector<std::pair<uint32_t, int64_t>> hits;
	/// Post-defence damage amounts used by the preview's combat event effects.
	std::vector<std::pair<uint32_t, int64_t>> resolvedHits;
	/// Targets receiving No Quarter after these hits, paired with remaining
	/// accepted activations before the projected Morale penalty expires.
	std::vector<std::pair<uint32_t, int32_t>> noQuarterTargets;
	/// Causal side/cause receipts survive recipient death/control changes and
	/// omission of the whole ignored No Quarter package. Replay in event order.
	std::vector<DefiantDenialProjection> defiantDenials;
};

/// Probe-only wall diagnostics. Never stored by a cache or battle model.
/// body counts only the outer cache operation; original->current fallback is
/// nested. preflight/dynamicDamage/remainder partition that body, not the probe.
struct DLL_LINKAGE DamageCacheProbeTiming
{
	using StageTiming = HypotheticLifecycleTiming::StageTiming;
	StageTiming body;
	StageTiming preflight;
	StageTiming dynamicDamage;
	damageCalculationTiming::Sample calculation;
	uint64_t remainderNanoseconds = 0;
	uint64_t getDamageCalls = 0;
	uint64_t getOriginalDamageCalls = 0;
	uint64_t rootCacheHits = 0;
	uint64_t localChildCacheHits = 0;
	uint64_t parentOriginalHits = 0;
	uint64_t rootOriginalFallbacks = 0;
	uint64_t parentOriginalFallbacks = 0;
	uint64_t dynamicBypasses = 0;
	uint64_t cacheFillCalls = 0;
	uint64_t deadReturns = 0;
	uint64_t failedOperations = 0;
	/// Reentry during a measured phase is included in that outer phase wall;
	/// its clocks/counters are suppressed to prevent overlap.
	uint64_t suppressedReentrantCalls = 0;
	// Call-local bookkeeping only, excluded from aggregation and serialization.
	uint32_t operationsInFlight = 0;
	uint32_t dynamicCallbacksInFlight = 0;
	uint32_t phasesInFlight = 0;

	void accumulate(const DamageCacheProbeTiming & sample)
	{
		body.accumulate(sample.body);
		preflight.accumulate(sample.preflight);
		dynamicDamage.accumulate(sample.dynamicDamage);
		calculation.accumulate(sample.calculation);
		remainderNanoseconds += sample.remainderNanoseconds;
		for(auto member : {&DamageCacheProbeTiming::getDamageCalls,
			&DamageCacheProbeTiming::getOriginalDamageCalls, &DamageCacheProbeTiming::rootCacheHits,
			&DamageCacheProbeTiming::localChildCacheHits, &DamageCacheProbeTiming::parentOriginalHits,
			&DamageCacheProbeTiming::rootOriginalFallbacks, &DamageCacheProbeTiming::parentOriginalFallbacks,
			&DamageCacheProbeTiming::dynamicBypasses, &DamageCacheProbeTiming::cacheFillCalls,
			&DamageCacheProbeTiming::deadReturns, &DamageCacheProbeTiming::failedOperations,
			&DamageCacheProbeTiming::suppressedReentrantCalls})
			(this->*member) += sample.*member;
	}
};

class DamageCache
{
private:
	std::unordered_map<uint32_t, std::unordered_map<uint32_t, float>> damageCache;
	std::map<BattleHex, std::unordered_map<uint32_t, int64_t>> obstacleDamage;
	std::set<uint32_t> rangedMarkTargets;
	std::set<uint32_t> temporaryCombatModifierUnits;
	std::set<uint32_t> ambusherAttackers;
	std::set<std::pair<uint32_t, BattleSide>> shadowAssaultTargetSides;
	std::set<uint32_t> nightProwlerAttackers;
	std::set<uint32_t> frozenTargets;
	std::set<uint32_t> intrinsicMdrTargets;
	std::set<uint32_t> intrinsicPdrTargets;
	const DamageCache * parent;

	void cacheDamageMeasured(const battle::Unit * attacker, const battle::Unit * defender,
		std::shared_ptr<CBattleInfoCallback> hb, DamageCacheProbeTiming * timing);
	void buildObstacleDamageCache(std::shared_ptr<HypotheticBattle> hb, BattleSide side);
	void buildUnitDamageCache(std::shared_ptr<HypotheticBattle> hb, BattleSide side, bool terminalPressure);
	bool tracksRangedMarks(uint32_t defenderId) const;
	bool tracksTemporaryCombatModifier(uint32_t defenderId) const;
	void rememberTemporaryCombatModifiers(const battle::Unit * attacker, const battle::Unit * defender);
	bool tracksAmbusher(uint32_t attackerId) const;
	bool tracksShadowAssault(uint32_t defenderId) const;
	bool tracksNightProwler(uint32_t attackerId) const;
	bool tracksFrozen(uint32_t defenderId) const;
	bool tracksIntrinsicReduction(uint32_t defenderId, bool physical) const;

public:
	/// Owning exact cache-chain identity for completed Regeneration baseline reuse only.
	class Snapshot
	{
		friend class DamageCache;
		std::vector<DamageCache> layers;
	};
	Snapshot captureSnapshot() const;
	bool matchesSnapshot(const Snapshot & snapshot) const;
	DamageCache() : parent(nullptr) {}
	DamageCache(const DamageCache * parent) : parent(parent) {}

	void cacheDamage(const battle::Unit * attacker, const battle::Unit * defender, std::shared_ptr<CBattleInfoCallback> hb);
	int64_t getDamage(const battle::Unit * attacker, const battle::Unit * defender, std::shared_ptr<CBattleInfoCallback> hb);
	int64_t getDamage(const battle::Unit * attacker, const battle::Unit * defender,
		std::shared_ptr<CBattleInfoCallback> hb, DamageCacheProbeTiming * timing,
		DamageCacheAdmission * mainProbeAdmission = nullptr);
	int64_t getObstacleDamage(const BattleHex & hex, const battle::Unit * defender) const;
	int64_t getOriginalDamage(const battle::Unit * attacker, const battle::Unit * defender, std::shared_ptr<CBattleInfoCallback> hb);
	int64_t getOriginalDamage(const battle::Unit * attacker, const battle::Unit * defender,
		std::shared_ptr<CBattleInfoCallback> hb, DamageCacheProbeTiming * timing);
	void buildDamageCache(std::shared_ptr<HypotheticBattle> hb, BattleSide side);
	/// Terminal creature-pressure root only: retain obstacles, omit pair priming
	/// only for attested pure damage and saved rules that force dynamic queries.
	/// Do not use this root as the parent of another DamageCache.
	void buildCreaturePressureCache(std::shared_ptr<HypotheticBattle> hb, BattleSide side);
};

/// <summary>
/// Evaluate attack value of one particular attack taking into account various effects like
/// retaliation, 2-hex breath, collateral damage, shooters blocked damage
/// </summary>
class VisibleCandidateWorld;

class AttackPossibility
{
public:
	BattleHex from; //tile from which we attack
	BattleHex dest; //tile which we attack
	BattleAttackInfo attack;
	bool perfectMoment = false;
	/// Actual generation-source owner; no hidden policy or complete-horizon claim.
	std::shared_ptr<const VisibleCandidateWorld> queryWorld;

	// Detached unit states borrow their bonus bearer. Keep a per-candidate
	// mark projection alive for as long as its returned states can be read.
	std::shared_ptr<HypotheticBattle> effectPreview;
	/// Local receipt distinguishes a newly completed return from a pre-existing target mark.
	std::optional<uint32_t> completedReturnMarkTarget;
	std::shared_ptr<battle::CUnitState> attackerState;

	std::vector<std::shared_ptr<battle::CUnitState>> affectedUnits;
	std::vector<FortuneStrikeProjection> fortuneStrikes;
	/// Opted-in Tactics preserves actual Rain of Arrows collateral separately.
	std::vector<sylvanTacticsAI::SecondaryHit> tacticsSecondaryHits;
	std::vector<ProjectedOverwatchHit> overwatchHits;
	/// Supplied only by the owner of the same validated ordinary movement prefix.
	bool validatedVoluntaryApproach = false;
	BattleHex validatedApproachOrigin;
	/// Actual HP restored by the canonical New Horizons Vampirism trigger during
	/// this exchange, grouped by the living stack that received the healing.
	std::vector<std::pair<uint32_t, int64_t>> vampirismHealingByUnit;
	int64_t preAttackDamage = 0;
	/// Gross temporary HP added to the original attacker by successful Soul Harvest.
	/// Selected replay repeats that gain; it must not mask unclassified reflected loss.
	int64_t soulHarvestHealthGained = 0;
	bool bulwarkMireGripTriggered = false;

	float defenderDamageReduce = 0;
	float attackerDamageReduce = 0; //usually by counter-attack
	float collateralDamageReduce = 0; // friendly fire (usually by two-hex attacks)
	/// Expected lost/gained control, separate from deterministic injury/state.
	float frozenControlValue = 0;
	float petrifyingBloodControlValue = 0;
	std::vector<petrifyingBloodAI::AttemptIds> deferredBloodAttempts;
	int64_t shootersBlockedDmg = 0;
	bool defenderDead = false;

	AttackPossibility(const BattleHex & from, const BattleHex & dest, const BattleAttackInfo & attack_);

	float damageDiff() const;
	float attackValue() const;
	float damageDiff(float positiveEffectMultiplier, float negativeEffectMultiplier) const;

	/// Match the authoritative attack sequence without changing the unit's innate
	/// count (the server separately adds the fighting hero's creature-specific grant).
	static int getAttackCount(const battle::Unit & attacker, bool shooting, const CBattleInfoCallback & state);

	static AttackPossibility evaluate(
		const BattleAttackInfo & attackInfo,
		BattleHex hex,
		DamageCache & damageCache,
		std::shared_ptr<CBattleInfoCallback> state, bool perfectMoment = false,
		std::optional<newHorizonsAerialSupremacy::Excursion> aerial = std::nullopt,
		const dreadCavalryAI::Action * acceptedDreadAction = nullptr, bool validatedVoluntaryApproach = false,
		BattleHex validatedApproachOrigin = BattleHex::INVALID);

	static float calculateDamageReduce(
		const battle::Unit * attacker,
		const battle::Unit * defender,
		uint64_t damageDealt,
		DamageCache & damageCache,
		std::shared_ptr<CBattleInfoCallback> cb);
	static float calculateDamageReduce(
		const battle::Unit * attacker,
		const battle::Unit * defender,
		uint64_t damageDealt,
		DamageCache & damageCache,
		std::shared_ptr<CBattleInfoCallback> cb,
		DamageCacheProbeTiming * timing);

private:
	static int64_t evaluateBlockedShootersDmg(
		const BattleAttackInfo & attackInfo,
		BattleHex hex,
		DamageCache & damageCache,
		std::shared_ptr<CBattleInfoCallback> state);
};
