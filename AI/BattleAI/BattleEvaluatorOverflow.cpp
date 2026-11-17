/*
 * BattleEvaluatorOverflow.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleEvaluator.h"
#include "AttackPossibility.h"
#include "PossibleSpellcast.h"
#include "ArcaneOverflowValue.h"
#include "HeroActionAdmission.h"
#include "ForecastHeroActionRouting.h"
#include "HeroSpellOutcomeValue.h"
#include "SearchBudget.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMetamagicOverflow.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include <cmath>

namespace
{
using Outcome = arcaneOverflowAI::Outcome;
using Native = newHorizonsMetamagic::OverflowProjectionOutcome;
struct NativeBranch
{
	std::shared_ptr<HypotheticBattle> board;
	HypotheticBattle::ProjectedSpellAllowance allowance;
	std::vector<BattleStackAttacked> injuries;
	std::vector<uint32_t> resolved;
	std::optional<newHorizonsMetamagic::OverflowPreview> spill;
};
Native nativeOutcome(Outcome outcome)
{
	switch(outcome)
	{
	case Outcome::PrimaryResisted: return Native::PrimaryResisted;
	case Outcome::PrimaryOnly: return Native::SpillResisted;
	case Outcome::BothLand: return Native::BothLand;
	}
	throw std::logic_error("Unknown finite Overflow outcome");
}
std::optional<NativeBranch> projectNativeBranch(std::shared_ptr<HypotheticBattle> board,
	const PossibleSpellcast & candidate, const CGHeroInstance * hero, BattleSide side, Outcome outcome)
{
	const auto admission = heroActionProjection::admitCandidate(*board, side, candidate);
	if(!admission.admitted || !admission.spellAllowance) return std::nullopt;
	const auto * originalTarget = candidate.dest.front().unitValue;
	const auto * target = board->battleGetUnitByID(originalTarget->unitId());
	if(!target) return std::nullopt;
	const auto targetId = target->unitId();
	spells::BattleCast cast(board.get(), hero, spells::Mode::HERO, candidate.spell);
	cast.setOvercharge(candidate.spellOvercharge);
	auto native = candidate.spell->battleMechanics(&cast);
	const spells::Target aim{spells::Destination(target)};
	if(!native->canBeCastAt(aim)) return std::nullopt;
	native->setMetamagicOverflowProjectionOutcome(nativeOutcome(outcome));
	native->castEval(board->getServerCallback(), aim);
	if(!board->projectAcceptedHeroSpell(side, candidate.spell->getId(), targetId,
		*admission.spellAllowance)) return std::nullopt;
	const auto injuries = native->getResolvedHeroInjuries();
	const auto resolved = native->getResolvedHeroRecipients();
	return NativeBranch{board, *admission.spellAllowance, {injuries.begin(), injuries.end()},
		{resolved.begin(), resolved.end()}, native->metamagicOverflowReceipt()};
}
float immediateInjuryValue(const NativeBranch & branch, const std::shared_ptr<HypotheticBattle> & before,
	PlayerColor player, DamageCache & cache, float positive, float negative, Outcome outcome)
{
	DamageCache local(&cache);
	const auto injuryValue = [&](uint32_t id, int64_t damage, std::optional<PlayerColor> actualOwner = std::nullopt)
	{
		const auto * victim = before->battleGetUnitByID(id);
		if(!victim || damage < 0) return std::numeric_limits<float>::quiet_NaN();
		battleSearch::require(before->searchBudget().get(), battleSearch::WorkKind::Node);
		const auto value = AttackPossibility::calculateDamageReduce(nullptr, victim,
			static_cast<uint64_t>(damage), local, before);
		const auto owner = actualOwner.value_or(before->battleGetOwner(victim));
		return owner != player ? value * positive : -4 * value * negative;
	};
	float result = 0;
	for(const auto & injury : branch.injuries)
		result += injuryValue(injury.stackAttacked, injury.damageAmount);
	if(outcome == Outcome::BothLand)
	{
		result += injuryValue(branch.spill->recipient, branch.spill->damageIfLands, branch.spill->recipientOwner);
		for(const auto & spill : branch.spill->additionalRecipients)
			result += injuryValue(spill.recipient, spill.damageIfLands, spill.recipientOwner);
	}
	return result;
}
bool sameNativePlan(const newHorizonsMetamagic::OverflowPreview & seed,
	const newHorizonsMetamagic::OverflowPreview & branch)
{
	return seed.additionalRecipients.size() == branch.additionalRecipients.size()
		&& seed.additionalRecipients.size() <= 1
		&& (seed.additionalRecipients.empty()
			|| (seed.additionalRecipients.front().additionalRecipients.empty()
				&& branch.additionalRecipients.front().additionalRecipients.empty()
				&& sameNativePlan(seed.additionalRecipients.front(), branch.additionalRecipients.front())))
		&& seed.primary == branch.primary && seed.recipient == branch.recipient
		&& seed.casterOwner == branch.casterOwner && seed.recipientOwner == branch.recipientOwner
		&& seed.primaryDamageIfLands == branch.primaryDamageIfLands
		&& seed.rawSpill == branch.rawSpill && seed.damageIfLands == branch.damageIfLands
		&& seed.defendedSecondaryDamage == branch.defendedSecondaryDamage
		&& seed.primaryResistancePercent == branch.primaryResistancePercent
		&& seed.secondaryResistancePercent == branch.secondaryResistancePercent && !branch.conditionalRebirth;
}
}

bool BattleEvaluator::prepareOverflowOutcome(PossibleSpellcast & candidate, spells::Mechanics & mechanics,
	uint32_t actorId)
{
	using namespace newHorizonsMetamagic;
	const auto support = overflowSupport(mechanics, candidate.dest, true);
	if(support == OverflowSupport::Inapplicable) return true;
	const auto incomplete = [&]()
	{
		candidate.spellRunicEstimatePartial = true;
		return false; // Unknown forecast, not a refusal of the native action.
	};
	if(support == OverflowSupport::Unsupported || !mechanics.canBeCastAt(candidate.dest)) return incomplete();
	// HOF/multi-recipient formulas are natively inapplicable. Preserve their
	// existing full distributions; never replace an existing expected-value lane.
	if(candidate.spellHandOfFateExpectedValue || candidate.spellBattleFormExpectedValue
		|| candidate.spellConfusionExpectedValue || candidate.spellPuppetMasterExpectedValue)
		return incomplete();
	const auto work = hb->searchBudget();
	if(!work) return incomplete();
	// One bounded native seed discovers the POST-primary-kill plan. Reserve the
	// maximum three native branches BEFORE any effects; this is not utility work.
	work->require(battleSearch::WorkKind::Node, 3);
	// Select the source BEFORE each native prefix. Only the exact still-open
	// unmodified decision snapshot can use its retained real reference source;
	// a modified nested source never redirects to that original battle.
	work->require(battleSearch::WorkKind::Node,
		static_cast<uint32_t>(hb->battleGetAllUnits(false).size()) + 1);
	const auto route = forecast::heroAction::classify(*hb, side, candidate).route;
	HypotheticBattle::Subject source = hb;
	if(route == forecast::heroAction::Route::CurrentNativeReference && hb->forecastState())
	{
		if(!decisionBaseOpen || !decisionBase || !decisionBase->forecastState() || !decisionNativeSource
			|| !hb->forecastState()->sameSnapshot(*decisionBase->forecastState())
			|| decisionNativeSource->battleGetRound() != hb->battleGetRound()
			|| decisionNativeSource->getBattle()->getActivationSerial() != hb->getActivationSerial()
			|| decisionNativeSource->getBattle()->getCapturedMagicRules() != hb->getCapturedMagicRules()) return incomplete();
		source = decisionNativeSource;
	}
	const auto project = [&](Outcome outcome)
	{
		auto board = forecast::heroAction::makeBranch(env.get(), source, work, route);
		return projectNativeBranch(std::move(board), candidate, mechanics.getHeroCaster(), side, outcome);
	};
	const auto seed = project(Outcome::BothLand);
	if(!seed) return incomplete();
	if(!seed->spill) return true; // Actual native pipeline produced no Overflow.
	const auto & plan = *seed->spill;
	if(plan.conditionalRebirth) return incomplete();
	if(!plan.additionalRecipients.empty()
		&& (plan.additionalRecipients.size() != 1
			|| (plan.primaryResistancePercent < 100 && (plan.secondaryResistancePercent != 0
				|| plan.additionalRecipients.front().secondaryResistancePercent != 0))
			|| plan.additionalRecipients.front().conditionalRebirth)) return incomplete();
	const auto outcomeValue = inspirationOutcomeEvaluator(candidate, mechanics, actorId);
	const auto kernel = heroSpellProjection::healthOutcomeKernel(playerID, work.get());
	const auto score = [&](const NativeBranch & branch, Outcome outcome) -> std::optional<arcaneOverflowAI::LocalUtility>
	{
		if(branch.allowance != seed->allowance) return std::nullopt;
		if(outcome == Outcome::PrimaryResisted)
		{
			if(branch.spill || !branch.injuries.empty() || !branch.resolved.empty()) return std::nullopt;
		}
		else if(!branch.spill || !sameNativePlan(plan, *branch.spill)) return std::nullopt;
		for(const auto & injury : branch.injuries)
			if(injury.stackAttacked != plan.primary) return std::nullopt;
		const auto immediate = immediateInjuryValue(branch, hb, playerID, damageCache,
			scoreEvaluator.getPositiveEffectMultiplier(), scoreEvaluator.getNegativeEffectMultiplier(), outcome);
		const auto completion = outcomeValue ? heroSpellProjection::evaluateLocalOutcome(outcomeValue,
			branch.board, branch.injuries, true, {branch.resolved, true}, kernel) : 0;
		return arcaneOverflowAI::LocalUtility{immediate, completion};
	};
	const auto seedValue = [&]() { return score(*seed, Outcome::BothLand); };
	const auto otherValue = [&](Outcome outcome) -> std::optional<arcaneOverflowAI::LocalUtility>
	{
		const auto branch = project(outcome);
		return branch ? score(*branch, outcome) : std::nullopt;
	};
	// Utility-visit charge is separate from native branch discovery. No catch:
	// BudgetExhausted reaches the existing legal-incumbent boundary unchanged.
	const auto expected = arcaneOverflowAI::expectedSeededLocalValue(plan, *work, seedValue, otherValue);
	if(!expected) return incomplete();
	float core = 0;
	if(outcomeValue)
	{
		// New Overflow keeps the existing nominal accepted-candidate continuation
		// convention once. HOF's independent core-only baseline is untouched.
		core = outcomeValue(seed->board, seed->injuries, true, false, {seed->resolved, true}).value;
		core -= heroSpellProjection::localReceiptMarginal(seed->board, kernel);
	}
	if(!std::isfinite(core)) return incomplete();
	candidate.spellOverflowExpectedValue = expected->immediate;
	candidate.spellInspirationExpectedValue = core + expected->completion;
	return true;
}
