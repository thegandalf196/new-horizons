/*
 * OriginalMeleeSequence.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "BraceStagePreparation.h"
#include "../../lib/battle/Unit.h"
#include <vector>

class InitialExportBattleSubject;

class BattleInfo;

namespace heroOrderValue
{
enum class MeleeStrikeKind { BRACE_PRIMARY, INCOMING_PRIMARY, ORDINARY_RETALIATION };
enum class MeleeSequenceStop
{
	INPUT_OR_MOVEMENT_UNAVAILABLE,
	HIDDEN_HERO_POLICY,
	UNSUPPORTED_REACTION_POLICY,
	REBIRTH_PENDING,
	UNKNOWN_LUCK_PENDING,
	NO_QUARTER_OR_DEFIANT_PENDING,
	SUPPORTED_SEQUENCE_FINISHED
};

struct MeleeHitReceipt
{
	MeleeStrikeKind kind;
	uint32_t actorId;
	uint32_t victimId;
	BattleHex actorPosition;
	BattleHex victimPosition;
	BattleHex selectedTargetHex;
	int32_t round;
	int32_t activeStackId;
	int32_t attackIndex;
	int distanceArgument;
	int64_t actorHealthBefore;
	int64_t victimHealthBefore;
	int64_t requestedDamage;
	int64_t appliedDamage;
	int64_t victimHealthAfter;
	battle::DamageProvenance provenance;
	bool damageRetaliation;
	bool reactiveForLastStand;
	bool lastStandTriggered;
	bool operator==(const MeleeHitReceipt &) const = default;
};

/// Original fixed melee loop, not another generated action or resampled attack.
/// Completed sequence ends BEFORE EndAction/activation/queue/Plague completion.
/// No scalar, immediate utility, baseline or completed exchange is supplied.
struct OriginalMeleeSequence
{
	explicit OriginalMeleeSequence(const BraceMeleeAction & action) : originalAction(action) {}
	BraceMeleeAction originalAction;
	std::shared_ptr<HypotheticBattle> branch;
	std::optional<BraceStageEntry> incomingEntry;
	std::vector<MeleeHitReceipt> hits;
	int originalAttackCount = 0;
	int incomingBlows = 0;
	MeleeSequenceStop stop = MeleeSequenceStop::INPUT_OR_MOVEMENT_UNAVAILABLE;
	bool meleeSequenceComplete() const { return stop == MeleeSequenceStop::SUPPORTED_SEQUENCE_FINISHED; }
	static constexpr bool activationComplete = false;
	static constexpr bool absoluteHorizonComplete = false;
};

/// Fresh producer only: never accepts an externally fabricated Pending receipt.
/// Requires supported visible policies and immutable original world ownership
/// for the call and retained branch. May leave an explicit private valid prefix.
OriginalMeleeSequence projectOriginalMeleeSequence(const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & beforeMovement, const BraceMeleeAction & action);

/// Explicit recipient-query model only; fresh producer shares the exact original
/// movement/loop/resource/hit path, but cannot prove originating publication.
/// Ordinary production routing remains a separate, still-required integration.
OriginalMeleeSequence projectRecipientMeleeSequence(const Environment * environment,
	const std::shared_ptr<InitialExportBattleSubject> & beforeMovement, const BraceMeleeAction & action);
OriginalMeleeSequence projectLegacyRecipientMeleeSequence(const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & beforeMovement, BattleInfo & native,
	const BraceMeleeAction & action);

/// Explicit full-information native source handle under its real immutable
/// interval. Preserves recipient-local history; unknown export provenance stops
/// at the existing pre-injury expiry-capture boundary AFTER genuine movement.
OriginalMeleeSequence projectLegacyOriginalMeleeSequence(const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & beforeMovement, BattleInfo & native,
	const BraceMeleeAction & action);
}
