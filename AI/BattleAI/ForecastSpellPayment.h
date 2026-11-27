/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "ForecastSideState.h"
#include <vcmi/ServerCallback.h>
#include <utility>
namespace forecast
{
struct SpellPaymentTicket
{
	BattleSide side;
	SpellID spell;
	int32_t exactCost;
	uint64_t actionEpoch;
	bool evaluated = false;
	std::vector<battle::spellPoints::Payment> refunds;
	int64_t residualLifeDamage = 0;
};
/// Synchronous evaluation contexts, never copied to a child forecast branch.
/// Every nested evaluation has its own frame, including automatic/free/reflected
/// casts. Only a completed manual frame can seal a ticket for its begun action.
class DLL_LINKAGE SpellPaymentScopes
{
	struct Frame
	{
		uint64_t id;
		SpellEvaluationKind kind;
		BattleSide side;
		SpellID spell;
		int32_t exactCost = 0;
		bool admitted = false;
		std::optional<uint64_t> actionEpoch;
		std::vector<battle::spellPoints::Payment> refunds;
		bool delegationAllowed = false, delegatedCompletion = false;
		std::optional<uint64_t> delegateParent;
		int64_t residualLifeDamage = 0;
	};
	std::vector<Frame> frames;
	uint64_t nextId = 1;
	bool valid = true;
public:
	SpellPaymentScopes() = default;
	SpellPaymentScopes(const SpellPaymentScopes &) = delete;
	SpellPaymentScopes & operator=(const SpellPaymentScopes &) = delete;
	uint64_t begin(SpellEvaluationKind kind, BattleSide side, SpellID spell, std::optional<uint64_t> actionEpoch);
	void allowDelegation(uint64_t id);
	bool completedDelegation() const;
	void admit(uint64_t id, const SideState * state, int32_t exactCost, bool admitted);
	std::optional<SpellPaymentTicket> finish(uint64_t id, bool completed) noexcept;
	bool queueResidualLife(BattleSide side, int64_t damage);
	bool queueRefund(BattleSide side, battle::spellPoints::Payment payment);
	std::optional<std::pair<BattleSide, uint64_t>> activeManualAction() const;
	SpellEvaluationKind currentKind() const;
	BattleSide currentSide() const;
	bool healthy() const { return valid; }
	bool empty() const { return frames.empty(); }
	void invalidate() noexcept { valid = false; frames.clear(); }
};
}
class HypotheticBattle;
class PossibleSpellcast;
namespace forecast
{
/// Terminal/synthetic paths call this before applying their custom unit effects.
/// The factory uses the native configured mechanics; no independent cost formula.
DLL_LINKAGE std::optional<int32_t> candidateSpellPaymentCost(HypotheticBattle &, BattleSide, const PossibleSpellcast &);
}
