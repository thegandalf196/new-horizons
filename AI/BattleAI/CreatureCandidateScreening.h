/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include <cstddef>
#include <cstdint>
#include "../../lib/battle/BattleHex.h"
#include <optional>
#include <span>
#include <vector>
class HypotheticBattle;
class PotentialTargets;
namespace battle { class Unit; }

namespace creatureCandidateScreening
{
/// Current visible ordinary-mechanics domain shared with movement screening.
/// Opposing hidden policy stays unknown; no hidden Hero address is inspected.
/// This certifies supported forecast inputs, never optimality or hidden rules.
bool supportsVisibleOrdinaryMelee(const battle::Unit * actor, const HypotheticBattle & state);

/// Intentional approximate normal-melee refinement, never an exchange bound.
/// Keeps the original seed, the maximum-value plateau, and waited incumbent.
/// Invalid estimates retain every candidate. No ranking or RNG reorders actions.
std::vector<bool> promisingNormalMelee(std::span<const float> immediate,
	std::optional<size_t> waitedIncumbent);

struct MeleeCandidatePosition
{
	uint32_t defenderId;
	BattleHex from;
};

/// Retain same-defender legal head positions within one hex of ORIGINAL anchors.
/// Newly retained positions never seed another expansion. No path queries occur.
/// Malformed geometry/mask retains the full pool; no contact-side claim is made.
std::vector<bool> retainLocalMeleeAlternatives(std::span<const MeleeCandidatePosition> positions,
	const std::vector<bool> & originalAnchors);

/// Whole-domain fallback for unknown ownership, rules, forced actions, effects,
/// heroes or candidate provenance. Returns a mask in the original vector order.
std::vector<bool> normalMeleeForecasts(const battle::Unit * actor,
	const PotentialTargets & targets, const HypotheticBattle & state,
	bool siegeDefense, float positiveMultiplier, float negativeMultiplier,
	std::optional<size_t> waitedIncumbent);
}
