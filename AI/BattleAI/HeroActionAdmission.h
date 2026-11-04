/*
 * HeroActionAdmission.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "HypotheticBattle.h"
#include "../../lib/battle/HeroOrderEffects.h"

#include <functional>

class PossibleSpellcast;

namespace heroActionProjection
{
/// Exact native getter/source eligibility for ONE immutable Hero actor batch.
/// No damage cache, action permission or hidden-policy knowledge. Discard before
/// applying an action or mutating the branch; every target rechecks roster/binding.
class CanonicalSourceAdmission final
{
	const battle::Unit & actor;
	const CBattleInfoCallback & battle;
	const BattleInfo * nativeWorld = nullptr;
	std::optional<bool> admitted;
public:
	CanonicalSourceAdmission(const battle::Unit & actor, const CBattleInfoCallback & battle)
		: actor(actor), battle(battle) {}
	CanonicalSourceAdmission(const CanonicalSourceAdmission &) = delete;
	CanonicalSourceAdmission & operator=(const CanonicalSourceAdmission &) = delete;
	bool admits(const battle::Unit & target);
};

struct Admission
{
	bool admitted = false;
	bool legacyOrder = false;
	std::optional<HypotheticBattle::ProjectedSpellAllowance> spellAllowance;
	std::optional<HypotheticBattle::ProjectedOrderAllowance> orderAllowance;
	std::optional<heroCommands::PreparedOrderEffects> orderEffects;
	HypotheticBattle::ProjectedCounterspellOutcome counterspell;
	bool counterspellNegated = false;
};

/// For an already generated candidate, prepare and begin its private action before
/// resolving the opposing ward. A refusal can retain a begun private action; the
/// caller must discard that candidate branch, not continue its valuation or commit.
/// This is neither command validation nor an authoritative action/mana mutation.
Admission admitCandidate(HypotheticBattle & state, BattleSide side, const PossibleSpellcast & candidate);

struct OrderCompletion
{
	bool accepted = false;
	std::optional<HypotheticBattle::ProjectedOrderContinuation> continuation;
};
/// Existing ordered finalization after an admitted Order. The ordinary full
/// evaluator owns its result; scouting must not request this continuation.
OrderCompletion completeOrder(HypotheticBattle & state, const CBattleInfoCallback & before,
	BattleSide side, const PossibleSpellcast & candidate, const Admission & admission,
	const std::function<void(const HeroOrderState &)> & afterAcceptedOrder = nullptr);

}
