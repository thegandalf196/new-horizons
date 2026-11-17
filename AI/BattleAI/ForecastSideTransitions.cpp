/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastSideTransitions.h"
#include "SearchBudget.h"
#include "../../lib/constants/NumericConstants.h"
namespace forecast
{
namespace
{
uint64_t charge(SideState & state, CastCost cost)
{
	if(!state.mana || !state.knowsSpell(cost.spell) || (cost.automatic && cost.amount != 0) || state.nextCastToken == std::numeric_limits<uint64_t>::max())
		throw std::invalid_argument("Unsupported compact forecast spell payment");
	const auto paid = battle::spellPoints::spend(*state.mana, cost.amount);
	if(!paid) throw std::invalid_argument("Unaffordable compact forecast spell payment");
	const auto token = state.nextCastToken++;
	state.castCharges.push_back({token, cost.spell, cost.automatic, *paid, {}});
	return token;
}
}
std::optional<SideReceipt> SideTransitions::stage(const std::shared_ptr<const SideState> & current,
	SideDirty dirty, const std::function<void(SideState &)> & transition,
	battleSearch::SearchBudget * budget, bool needsHero)
{
	if(!current || (needsHero && !current->hasVisibleHero())) return std::nullopt;
	battleSearch::require(budget, battleSearch::WorkKind::Node);
	try
	{
		current->validate();
		if(current->revision == std::numeric_limits<uint64_t>::max()) return std::nullopt;
		auto next = std::make_shared<SideState>(current->fork());
		transition(*next);
		++next->revision; next->validate();
		SideReceipt receipt; receipt.before = current; receipt.next = std::move(next); receipt.dirty = dirty;
		return receipt;
	}
	catch(const std::exception &) { return std::nullopt; }
}
bool SideTransitions::commit(std::shared_ptr<const SideState> & current, const SideReceipt & receipt) noexcept
{
	if(!current || current != receipt.before || !receipt.next) return false;
	current = receipt.next;
	return true;
}
std::optional<SideReceipt> SideTransitions::acceptedAction(const std::shared_ptr<const SideState> & current,
	const HeroActionAllowanceState::Receipt & action, const HeroActionAllowanceState & consumed,
	bool lightSpell, uint8_t maximumPairs, const std::vector<uint32_t> & recipients,
	std::optional<CastCost> cost, battleSearch::SearchBudget * budget, std::span<const battle::spellPoints::Payment> refunds)
{
	std::vector<uint32_t> overlap;
	std::optional<uint64_t> token;
	auto receipt = stage(current, SideDirty::Actions | (cost ? SideDirty::Resources : SideDirty::None), [&](SideState & next)
	{
		if(action.round != next.round || !next.ledger
			|| (cost && (cost->automatic || action.action != HeroActionAllowanceState::ActionKind::SPELL)))
			throw std::invalid_argument("Invalid compact forecast accepted action");
		auto & ledger = next.ledger->allowances;
		if(ledger.currentRound >= 0)
		{
			const auto native = ledger.consumeAllowance(action.grantId, action.action, next.round,
				[&](const HeroActionAllowanceState::Grant & grant) { return grant.id == action.grantId; });
			if(!native || *native != action || ledger != consumed)
				throw std::invalid_argument("Stale compact forecast action receipt");
			overlap = DivineMandateTransition::applyAcceptedAction(ledger, action, next.round, lightSpell, maximumPairs, recipients);
		}
		else if(action.grantId != 0 || consumed != ledger)
			throw std::invalid_argument("Invalid legacy compact forecast action receipt");
		if(!cost && !refunds.empty()) throw std::invalid_argument("Refund without original cast payment");
		if(cost)
		{
			token = charge(next, *cost);
			auto & original = next.castCharges.back();
			for(const auto & refund : refunds)
				if(!battle::spellPoints::refundCharge(*next.mana, original.paid, original.refunded, refund))
					throw std::invalid_argument("Scoped cast refund exceeds actual payment provenance");
		}
	}, budget);
	if(receipt) { receipt->mandateOverlap = std::move(overlap); receipt->paymentToken = token; }
	return receipt;
}
std::optional<SideReceipt> SideTransitions::payCast(const std::shared_ptr<const SideState> & current,
	CastCost cost, battleSearch::SearchBudget * budget)
{
	std::optional<uint64_t> token;
	auto receipt = stage(current, SideDirty::Resources, [&](SideState & next) { token = charge(next, cost); }, budget);
	if(receipt) receipt->paymentToken = token;
	return receipt;
}
std::optional<SideReceipt> SideTransitions::refundCast(const std::shared_ptr<const SideState> & current,
	uint64_t token, battle::spellPoints::Payment payment, battleSearch::SearchBudget * budget)
{
	return stage(current, SideDirty::Resources, [&](SideState & next)
	{
		const auto charge = std::find_if(next.castCharges.begin(), next.castCharges.end(),
			[token](const CastCharge & charge) { return charge.token == token; });
		if(charge == next.castCharges.end() || !next.mana
			|| !battle::spellPoints::refundCharge(*next.mana, charge->paid, charge->refunded, payment))
			throw std::invalid_argument("Invalid compact forecast cast refund");
	}, budget);
}
std::optional<SideReceipt> SideTransitions::restoreNormal(const std::shared_ptr<const SideState> & current,
	int32_t amount, battleSearch::SearchBudget * budget)
{
	return stage(current, SideDirty::Resources, [&](SideState & next)
	{
		if(!next.mana || !next.mana->pools.restoreNormal(amount, next.mana->normalLimit()))
			throw std::invalid_argument("Invalid compact forecast Normal Mana restoration");
	}, budget);
}
std::optional<SideReceipt> SideTransitions::replaceMana(const std::shared_ptr<const SideState> & current,
	const battle::spellPoints::State & expected, battle::spellPoints::State after, battleSearch::SearchBudget * budget)
{
	return stage(current, SideDirty::Resources, [&](SideState & next)
	{
		if(!next.mana || *next.mana != expected || after.normalCapacity != expected.normalCapacity || after.twoPools != expected.twoPools)
			throw std::invalid_argument("Stale compact forecast resource transition");
		after.validate(); next.mana = std::move(after);
	}, budget);
}
std::optional<SideReceipt> SideTransitions::recordWisdom(const std::shared_ptr<const SideState> & current,
	SpellID spell, int32_t round, battleSearch::SearchBudget * budget)
{
	return stage(current, SideDirty::Completion, [&](SideState & next)
	{
		if(!next.ledger || round != next.round) throw std::invalid_argument("Invalid compact forecast Wisdom round");
		next.ledger->wisdom.recordCompletedCast(spell, round);
	}, budget);
}
std::optional<SideReceipt> SideTransitions::recordCompletion(const std::shared_ptr<const SideState> & current,
	int32_t level, battleSearch::SearchBudget * budget, std::optional<SpellID> automaticSpell)
{
	std::optional<uint64_t> token;
	auto receipt = stage(current, SideDirty::Completion, [&](SideState & next)
	{
		if(automaticSpell) token = charge(next, CastCost{*automaticSpell, 0, true});
		auto & ledger = *next.ledger;
		if(ledger.completedCastCount)
		{
			if(*ledger.completedCastCount == std::numeric_limits<int32_t>::max()) throw std::overflow_error("Compact cast count exhausted");
			++*ledger.completedCastCount;
		}
		ledger.completedHeroCast = true;
		if(level >= 1 && level <= GameConstants::SPELL_LEVELS) ledger.completedLevelMask |= 1u << (level - 1);
	}, budget);
	if(receipt) receipt->paymentToken = token;
	return receipt;
}
std::optional<SideReceipt> SideTransitions::setResponse(const std::shared_ptr<const SideState> & current,
	SpellResponseState value, battleSearch::SearchBudget * budget)
{
	return stage(current, SideDirty::Completion, [&](SideState & next)
	{
		value.validate();
		if(value.hasState() && !value.isReadyAt(next.round)) throw std::invalid_argument("Expired compact Spell Response");
		next.ledger->response = value;
	}, budget);
}
std::optional<SideReceipt> SideTransitions::setOverwhelming(const std::shared_ptr<const SideState> & current,
	OverwhelmingFormulaState value, battleSearch::SearchBudget * budget)
{
	return stage(current, SideDirty::Completion, [&](SideState & next) { value.validateShape(); next.ledger->overwhelming = value; }, budget);
}
std::optional<SideReceipt> SideTransitions::setAttunement(const std::shared_ptr<const SideState> & current,
	uint8_t mask, battleSearch::SearchBudget * budget)
{
	return stage(current, SideDirty::UnitStats | SideDirty::HealthCapacity | SideDirty::Retaliation | SideDirty::SpellDefense,
		[&](SideState & next)
		{
			if((mask & ~newHorizonsElementalAttunement::ALL_ELEMENTS) != 0
				|| (!next.attunementKnown && mask != next.attunementMask))
				throw std::invalid_argument("Unknown compact elemental activation");
			next.attunementMask = mask;
		}, budget, false);
}
std::optional<SideReceipt> SideTransitions::advanceRound(const std::shared_ptr<const SideState> & current,
	int32_t round, battleSearch::SearchBudget * budget)
{
	return stage(current, SideDirty::Round | SideDirty::Actions | SideDirty::Completion, [&](SideState & next)
	{
		if(round < next.round) throw std::invalid_argument("Reversed compact side round");
		if(next.ledger)
		{
			if(round != next.round && next.ledger->completedCastCount) next.ledger->completedCastCount = 0;
			if(next.typedActions || next.ledger->allowances.currentRound >= 0) next.ledger->allowances.resetForRound(round);
			if(next.ledger->response.hasState() && !next.ledger->response.isReadyAt(round)) next.ledger->response = {};
		}
		next.round = round;
	}, budget, false);
}
}
