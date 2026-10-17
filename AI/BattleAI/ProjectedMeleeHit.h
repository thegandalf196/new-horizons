/*
 * ProjectedMeleeHit.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "OriginalMeleeSequence.h"
class ProjectedMeleeExpiry;

namespace projectedMeleeHit
{
struct Request
{
	heroOrderValue::MeleeStrikeKind kind;
	uint32_t actorId;
	uint32_t victimId;
	BattleHex selectedTargetHex;
	int attackIndex;
	int distanceArgument;
	std::optional<int64_t> capturedBraceRequest = std::nullopt;
};

/// Caller has established a genuine ordered stage and supported participant
/// policies. Does not move units, generate actions or complete an activation.
/// Before-hit uncertainty refuses without injury; no RNG is called/resampled.
std::optional<heroOrderValue::MeleeHitReceipt> apply(HypotheticBattle & battle, ProjectedMeleeExpiry & expiry,
	const Request & request, heroOrderValue::MeleeSequenceStop & unavailableReason);
}
