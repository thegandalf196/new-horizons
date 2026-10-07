/*
 * NewHorizonsArmorer.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "../bonuses/Bonus.h"

class CGHeroInstance;

namespace battle
{
class Unit;
}

namespace newHorizonsArmorer
{
constexpr char SKILL_ID[] = "new-horizons:armorer";
constexpr char LAST_STAND_PERK_ID[] = "new-horizons:armorer.lastStand";

struct DLL_LINKAGE ArmorerLastStandDamageResult
{
	int64_t damageToApply = 0;
	bool triggered = false;
};

struct DLL_LINKAGE DefendStance
{
	std::vector<Bonus> bonuses;
	int32_t defenseIncrease = 0;
	int32_t meleeDefenseBonus = 0;
	int32_t rangedDefenseBonus = 0;
	bool holdFastApplied = false;
	std::optional<Bonus> holdFastBonus;
};

DLL_LINKAGE bool hasLastStand(const CGHeroInstance * hero);
DLL_LINKAGE bool canTriggerLastStand(const CGHeroInstance * hero, const battle::Unit * target);
/// Identifies an ordinary physical creature-attack event. Call only from the
/// actual attack resolution path; source provenance alone is not sufficient.
DLL_LINKAGE bool isEligiblePhysicalAttack(const battle::Unit * attacker, bool physicalDamage, bool spellLike);
/// Builds the shared passive/voluntary Defend bonus payload and explicit
/// melee/ranged provenance values from the live stack's existing bonuses.
/// `holdFastApplies` is the caller's canonical-rules/perk eligibility result.
/// The helper appends the existing Hold Fast bonus if it is not already present.
DLL_LINKAGE DefendStance buildDefendStance(const battle::Unit * stack, bool holdFastApplies = false);

/// Caps an otherwise lethal physical hit after accounting for Guardian Spirit.
/// `totalAvailableHealth` is the ordinary CHealth available pool (including
/// CHealth-tracked temporary HP, but excluding Guardian Spirit). Guardian Spirit
/// is supplied separately and consumed once by this calculation. Damage still
/// flows through normal health, casualty, provenance, and Rebirth handling.
DLL_LINKAGE ArmorerLastStandDamageResult resolveLastStandDamage(
	int64_t incomingDamage, int64_t guardianSpiritHitPoints, int32_t guardianSpiritRounds,
	int64_t totalAvailableHealth, bool eligible, bool sideAlreadyUsed);
}
