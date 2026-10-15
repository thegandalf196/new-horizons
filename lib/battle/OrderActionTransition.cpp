/*
 * OrderActionTransition.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "OrderActionTransition.h"

namespace orderActionTransition
{
std::optional<DoubleCommandState> preparePrimaryDoubleCommand(
	const DoubleCommandState & before, const Ledger::Receipt & receipt,
	bool activePerk, HeroCommand command, int32_t round, uint32_t anchor,
	uint32_t deferredSecondWindTarget)
{
	auto next = before;
	if(!next.begin(receipt, activePerk, command, round, anchor, deferredSecondWindTarget))
		return {};
	return next;
}

Ledger::Receipt provisionalReceipt(const Ledger::Selection & selection,
	int32_t receiptRound)
{
	return {selection.grantId, Ledger::ActionKind::ORDER,
		selection.allowance, selection.source, receiptRound, {}};
}

DoubleCommandState completeDoubleCommand(const DoubleCommandState & before,
	HeroCommand command)
{
	auto next = before;
	next.completeFollowup(command);
	return next;
}

void exhaustDoubleCommand(DoubleCommandState & continuation, Ledger & ledger,
	int32_t round)
{
	continuation.exhaustPendingOrder();
	std::erase_if(ledger.grants, [round](const Ledger::Grant & grant)
	{
		return grant.source == Ledger::GrantSource::DOUBLE_COMMAND
			&& grant.grantedRound == round;
	});
}

void validateHeroicSpiritCapacity(const Ledger & staged,
	const Ledger::Receipt & receipt)
{
	if(!staged.canResolveHeroicSpirit(receipt))
		throw std::runtime_error("Heroic Spirit Order cannot fit in the remaining action allowance identity space");
	// Reserve the possible success on a second detached ledger too, so count/
	// identity constraints fail before RNG even if this draw would fail.
	auto success = staged;
	if(!success.resolveHeroicSpirit(receipt, true))
		throw std::runtime_error("Heroic Spirit Order capacity cannot be reserved");
}
}
