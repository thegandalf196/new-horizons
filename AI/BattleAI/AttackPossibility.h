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
#include <algorithm>
#include <set>
#include <utility>
#include "../../lib/battle/CUnitState.h"
#include "StackWithBonuses.h"

#define BATTLE_TRACE_LEVEL 0

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
	/// nullopt retains legacy inference; a captured UNKNOWN must stay unknown
	/// when a consumed Gambler bonus changes the next strike's Luck value.
	std::optional<ProjectedLuckOutcome> resolvedLuck;
	/// Capture before damage: death or post-hit status changes cannot undo an accepted use.
	bool perfectFortune = false;
	BattleSide perfectFortuneSide = BattleSide::NONE;
	uint32_t attackerId = 0;
	uint32_t defenderId = 0;
	bool shooting = false;
	bool retaliation = false;
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

class DamageCache
{
private:
	std::unordered_map<uint32_t, std::unordered_map<uint32_t, float>> damageCache;
	std::map<BattleHex, std::unordered_map<uint32_t, int64_t>> obstacleDamage;
	std::set<uint32_t> rangedMarkTargets;
	std::set<uint32_t> evasiveShroudTargets;
	std::set<uint32_t> ambusherAttackers;
	std::set<std::pair<uint32_t, BattleSide>> shadowAssaultTargetSides;
	std::set<uint32_t> nightProwlerAttackers;
	DamageCache * parent;

	void buildObstacleDamageCache(std::shared_ptr<HypotheticBattle> hb, BattleSide side);
	bool tracksRangedMarks(uint32_t defenderId) const;
	bool tracksEvasiveShroud(uint32_t defenderId) const;
	bool tracksAmbusher(uint32_t attackerId) const;
	bool tracksShadowAssault(uint32_t defenderId) const;
	bool tracksNightProwler(uint32_t attackerId) const;

public:
	DamageCache() : parent(nullptr) {}
	DamageCache(DamageCache * parent) : parent(parent) {}

	void cacheDamage(const battle::Unit * attacker, const battle::Unit * defender, std::shared_ptr<CBattleInfoCallback> hb);
	int64_t getDamage(const battle::Unit * attacker, const battle::Unit * defender, std::shared_ptr<CBattleInfoCallback> hb);
	int64_t getObstacleDamage(const BattleHex & hex, const battle::Unit * defender);
	int64_t getOriginalDamage(const battle::Unit * attacker, const battle::Unit * defender, std::shared_ptr<CBattleInfoCallback> hb);
	void buildDamageCache(std::shared_ptr<HypotheticBattle> hb, BattleSide side);
};

/// <summary>
/// Evaluate attack value of one particular attack taking into account various effects like
/// retaliation, 2-hex breath, collateral damage, shooters blocked damage
/// </summary>
class AttackPossibility
{
public:
	BattleHex from; //tile from which we attack
	BattleHex dest; //tile which we attack
	BattleAttackInfo attack;
	bool perfectMoment = false;

	// Detached unit states borrow their bonus bearer. Keep a per-candidate
	// mark projection alive for as long as its returned states can be read.
	std::shared_ptr<HypotheticBattle> effectPreview;
	std::shared_ptr<battle::CUnitState> attackerState;

	std::vector<std::shared_ptr<battle::CUnitState>> affectedUnits;
	std::vector<FortuneStrikeProjection> fortuneStrikes;
	/// Actual HP restored by the canonical New Horizons Vampirism trigger during
	/// this exchange, grouped by the living stack that received the healing.
	std::vector<std::pair<uint32_t, int64_t>> vampirismHealingByUnit;
	int64_t preAttackDamage = 0;
	bool bulwarkMireGripTriggered = false;

	float defenderDamageReduce = 0;
	float attackerDamageReduce = 0; //usually by counter-attack
	float collateralDamageReduce = 0; // friendly fire (usually by two-hex attacks)
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
		std::shared_ptr<CBattleInfoCallback> state, bool perfectMoment = false);

	static float calculateDamageReduce(
		const battle::Unit * attacker,
		const battle::Unit * defender,
		uint64_t damageDealt,
		DamageCache & damageCache,
		std::shared_ptr<CBattleInfoCallback> cb);

private:
	static int64_t evaluateBlockedShootersDmg(
		const BattleAttackInfo & attackInfo,
		BattleHex hex,
		DamageCache & damageCache,
		std::shared_ptr<CBattleInfoCallback> state);
};
