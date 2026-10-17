/*
 * BracePreemptiveForecast.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../../lib/battle/BattleHex.h"
#include "../../lib/battle/IBattleInfoCallback.h"
#include "../../lib/battle/Unit.h"

#include <optional>

class CBattleInfoCallback;

namespace heroOrderValue
{
/// Entry to makeAttack's Brace stage: movement, Protect, before-attack spells
/// and Battlecraft have already resolved. IDs identify CURRENT callback units;
/// this is not a movement/range prediction or an entire activation forecast.
struct BraceStageEntry
{
	uint32_t incomingAttackerId;
	uint32_t bracedDefenderId;
	int voluntaryMovementDistance;
	bool ranged = false;
	bool counterDispatch = false;
};

struct BraceStageEvent
{
	uint32_t attackerId;
	uint32_t victimId;
	BattleHex attackerPosition;
	BattleHex victimPosition;
	int32_t round;
	int voluntaryMovementDistance;
	int64_t attackerHealthAtEntry;
	int64_t victimHealthAtEntry;
	// Server routing is counter=true/retaliation=true/brace=true, but both the
	// damage retaliation flag and normal retaliation consumption are FALSE.
	static constexpr bool normalRetaliation = false;
	static constexpr bool reactionForLastStand = true;
	battle::DamageProvenance damageProvenance;
};

struct BracePrimaryHit
{
	DamageEstimation damageRange;
	int64_t expectedRequestedDamage;
	int64_t damageAfterLastStand;
	int64_t expectedPrimaryDamageAmount;
	int64_t victimHealthAfterPrimary;
	int64_t guardianSpiritAfterPrimary;
	bool lastStandTriggered;
	bool lastStandEndsIncomingActivation;
};

enum class BraceStageCoverage
{
	NOT_TRIGGERED,
	BLOCKED_AT_ENTRY,
	NEEDS_PRE_HIT_PROJECTION,
	CONDITIONAL_PRIMARY_HIT
};

/// Owned values only; no borrowed Unit/provider survives this call. Even a
/// primary hit is NOT a completed reaction/continuation or selector utility.
/// Post-hit effects, collateral, Rebirth, Fortune aftermath, Defend and the
/// remaining incoming blow must be composed by a future ordered forecast.
struct BracePreemptiveForecast
{
	BraceStageCoverage coverage = BraceStageCoverage::NOT_TRIGGERED;
	std::optional<BraceStageEvent> event;
	std::optional<BracePrimaryHit> primary;
	static constexpr bool continuationComplete = false;
};

/// Estimates one primary blow with existing expected-Luck and health rules:
/// health transition of expected damage, NOT an exact distribution expectation.
/// Unprojected recursive Protect or combat-event triggers explicitly refuse a
/// primary estimate, as do independent attack-flag damage rolls. No live or
/// detached branch is mutated; no RNG is drawn. Entry snapshot must outlive call.
BracePreemptiveForecast forecastBraceAtStage(const CBattleInfoCallback & battle,
	const BraceStageEntry & entry);
}
