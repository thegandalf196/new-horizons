/*
 * OrderActionTransition.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../../Global.h"
#include "HeroActionAllowanceState.h"

/// Deterministic Order payment/continuation composition, not action admission.
/// Callers own policy predicates, staged ledgers, validation and commit timing.
namespace orderActionTransition
{
using Ledger = HeroActionAllowanceState;

template<typename GrantPredicate>
std::optional<Ledger::Selection> select(const Ledger & ledger, int32_t round,
	const GrantPredicate & grantPredicate)
{
	return ledger.eligibleAllowance(Ledger::ActionKind::ORDER, round, grantPredicate);
}

/// Mutates ONLY the supplied staged ledger. No implicit copy/reset/rollback.
/// In particular, an existing exposed staging outparameter stays exposed.
template<typename GrantPredicate>
std::optional<Ledger::Receipt> consume(Ledger & staged, uint32_t selectedGrantId,
	int32_t round, const GrantPredicate & grantPredicate)
{
	return staged.consumeAllowance(selectedGrantId, Ledger::ActionKind::ORDER,
		round, grantPredicate);
}

/// Does not imply a selected perk: false still calls begin, whose shape check
/// precedes its inactive-perk return. Do not replace this with a caller guard.
DLL_LINKAGE std::optional<DoubleCommandState> preparePrimaryDoubleCommand(
	const DoubleCommandState & before, const Ledger::Receipt & receipt,
	bool activePerk, HeroCommand command, int32_t round, uint32_t anchor,
	uint32_t deferredSecondWindTarget);

/// Construct at the original pre-spending checkpoint. Keep receipt-round and
/// begin-round reads separate; AI passes its already consumed receipt instead.
DLL_LINKAGE Ledger::Receipt provisionalReceipt(const Ledger::Selection & selection,
	int32_t receiptRound);

DLL_LINKAGE DoubleCommandState completeDoubleCommand(
	const DoubleCommandState & before, HeroCommand command);

/// Called only at the existing no-distinct-alternative checkpoint, AFTER the
/// grant and all current candidate queries. Mutation/erase ordering is fixed.
DLL_LINKAGE void exhaustDoubleCommand(DoubleCommandState & continuation,
	Ledger & ledger, int32_t round);

/// Called only after the caller establishes Heroic Spirit eligibility and
/// stages Mandate/mandatory companion grants. Reserves success on a copy only.
DLL_LINKAGE void validateHeroicSpiritCapacity(const Ledger & staged,
	const Ledger::Receipt & receipt);
}
