/*
 * BraceStagePreparation.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "BracePreemptiveForecast.h"
#include <memory>

class Environment;
class HypotheticBattle;

namespace heroOrderValue
{
/// A requested fresh ordinary melee candidate, not an already accepted action.
/// Distance/stage are DERIVED, never asserted by the caller. Nonordinary
/// continuation/perfect-action contexts require their original future owner.
struct BraceMeleeAction
{
	uint32_t incomingAttackerId;
	uint32_t requestedDefenderId;
	BattleHex attackFrom;
	BattleHex targetHex;
};

struct PreparedBraceStage
{
	std::shared_ptr<HypotheticBattle> branch;
	BraceStageEntry entry;
};

/// Derive a legal available route, move using canonical detached movement, and
/// establish that earlier First Strike/Protect/spell/Battlecraft stages are
/// no-ops. Unsupported prefixes return absence, never zero utility or pruning.
/// Legacy sources require raw movement invariance. Actual canonical-owned
/// recipients re-evaluate their complete original graph at current positions.
/// This intermediate bridge still refuses movement hits and divergent native
/// typed-hero bonus identities; it does NOT project arbitrary hero semantics.
/// Caller retains the immutable original world/bonus graph for this whole call
/// and every returned branch's lifetime; no ownership lease is acquired here.
std::optional<PreparedBraceStage> prepareBraceStage(const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & beforeMovement, const BraceMeleeAction & action);
}
