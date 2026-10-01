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

/// One physical attack (or its retaliation) in the read-only attack preview.
/// Keeping these deltas lets a committed preview replay Fortune aftermath at
/// the same boundaries as the authoritative action instead of collapsing a
/// multi-strike exchange into one post-hoc transition.
struct FortuneStrikeProjection
{
	uint32_t attackerId = 0;
	uint32_t defenderId = 0;
	bool shooting = false;
	bool retaliation = false;
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
};

class DamageCache
{
private:
	std::unordered_map<uint32_t, std::unordered_map<uint32_t, float>> damageCache;
	std::map<BattleHex, std::unordered_map<uint32_t, int64_t>> obstacleDamage;
	std::set<uint32_t> rangedMarkTargets;
	DamageCache * parent;

	void buildObstacleDamageCache(std::shared_ptr<HypotheticBattle> hb, BattleSide side);
	bool tracksRangedMarks(uint32_t defenderId) const;

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
