/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastSpellPayment.h"
#include "HypotheticBattle.h"
#include "PossibleSpellcast.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
namespace forecast
{
uint64_t SpellPaymentScopes::begin(SpellEvaluationKind kind, BattleSide side, SpellID spell,
	std::optional<uint64_t> actionEpoch)
{
	if(!valid || nextId == std::numeric_limits<uint64_t>::max()
		|| (kind != SpellEvaluationKind::OTHER && kind != SpellEvaluationKind::MANUAL_HERO
			&& kind != SpellEvaluationKind::AUTOMATIC_HERO))
		throw std::invalid_argument("Invalid spell-evaluation payment scope");
	std::optional<uint64_t> delegateParent;
	if(!frames.empty() && frames.back().delegationAllowed)
	{
		frames.back().delegationAllowed = false;
		if(kind != SpellEvaluationKind::OTHER && frames.back().kind == kind
			&& frames.back().side == side && frames.back().actionEpoch == actionEpoch)
			delegateParent = frames.back().id;
	}
	if(kind == SpellEvaluationKind::MANUAL_HERO && actionEpoch)
		for(const auto & frame : frames)
			if(frame.kind == kind && frame.side == side && frame.actionEpoch == actionEpoch
				&& (frame.admitted || delegateParent != frame.id))
				throw std::invalid_argument("Manual recursion lacks native concentration delegation");
	const auto id = nextId++;
	frames.push_back({id, kind, side, spell, 0, false, actionEpoch, {}, false, false, delegateParent});
	return id;
}
void SpellPaymentScopes::allowDelegation(uint64_t id)
{
	if(!valid || frames.empty() || frames.back().id != id || frames.back().admitted
		|| frames.back().kind == SpellEvaluationKind::OTHER || frames.back().delegationAllowed)
		throw std::invalid_argument("Invalid native concentration delegation");
	frames.back().delegationAllowed = true;
}
bool SpellPaymentScopes::completedDelegation() const
{ return valid && !frames.empty() && frames.back().delegatedCompletion; }
void SpellPaymentScopes::admit(uint64_t id, const SideState * state, int32_t exactCost, bool admitted)
{
	if(!valid || frames.empty() || frames.back().id != id || frames.back().admitted)
		throw std::invalid_argument("Stale spell-evaluation admission");
	auto & frame = frames.back();
	if(!admitted || exactCost < 0 || (frame.kind != SpellEvaluationKind::MANUAL_HERO && exactCost != 0))
		throw std::invalid_argument("Rejected spell-evaluation admission");
	if(frame.kind != SpellEvaluationKind::OTHER)
	{
		if(!state || !state->hasVisibleHero() || !state->mana || state->side != frame.side
			|| !state->knowsSpell(frame.spell) || state->mana->pools.getTotal() < exactCost)
			throw std::invalid_argument("Unsupported or unaffordable admitted spell evaluation");
		state->validate();
	}
	frame.exactCost = exactCost; frame.admitted = true;
}
SpellEvaluationKind SpellPaymentScopes::currentKind() const
{ return frames.empty() ? SpellEvaluationKind::OTHER : frames.back().kind; }
BattleSide SpellPaymentScopes::currentSide() const
{ return frames.empty() ? BattleSide::NONE : frames.back().side; }
std::optional<SpellPaymentTicket> SpellPaymentScopes::finish(uint64_t id, bool completed) noexcept
{
	if(!valid || frames.empty() || frames.back().id != id) { invalidate(); return std::nullopt; }
	auto frame = std::move(frames.back()); frames.pop_back();
	if(!completed || !frame.admitted || frame.kind != SpellEvaluationKind::MANUAL_HERO || !frame.actionEpoch) return std::nullopt;
	if(frame.delegateParent)
	{
		if(frames.empty() || frames.back().id != *frame.delegateParent) { invalidate(); return std::nullopt; }
		frames.back().delegatedCompletion = true;
	}
	return SpellPaymentTicket{frame.side, frame.spell, frame.exactCost, *frame.actionEpoch, true, std::move(frame.refunds), frame.residualLifeDamage};
}
std::optional<std::pair<BattleSide, uint64_t>> SpellPaymentScopes::activeManualAction() const
{
	if(!valid || frames.empty() || frames.back().kind != SpellEvaluationKind::MANUAL_HERO || !frames.back().actionEpoch)
		return std::nullopt;
	return std::pair{frames.back().side, *frames.back().actionEpoch};
}
bool SpellPaymentScopes::queueResidualLife(BattleSide side, int64_t damage)
{
	if(!valid || frames.empty() || damage < 0) return false;
	auto & frame = frames.back();
	if(!frame.admitted || frame.kind != SpellEvaluationKind::MANUAL_HERO || frame.side != side || !frame.actionEpoch
		|| damage > std::numeric_limits<int64_t>::max() - frame.residualLifeDamage) return false;
	frame.residualLifeDamage += damage;
	return true;
}
bool SpellPaymentScopes::queueRefund(BattleSide side, battle::spellPoints::Payment payment)
{
	payment.validate();
	if(!valid || frames.empty()) return false;
	auto & frame = frames.back();
	if(!frame.admitted || frame.kind != SpellEvaluationKind::MANUAL_HERO || frame.side != side || !frame.actionEpoch) return false;
	int64_t total = payment.total();
	for(const auto & pending : frame.refunds) total += pending.total();
	if(total > frame.exactCost) return false;
	frame.refunds.push_back(payment);
	return true;
}
std::optional<int32_t> candidateSpellPaymentCost(HypotheticBattle & state, BattleSide side, const PossibleSpellcast & candidate)
{
	if(!candidate.spell) return std::nullopt;
	const auto * hero = state.battleGetFightingHero(side);
	if(!hero) return std::nullopt;
	auto target = candidate.dest;
	for(auto & destination : target)
		if(destination.unitValue)
		{
			destination.unitValue = state.battleGetUnitByID(destination.unitValue->unitId());
			if(!destination.unitValue) return std::nullopt;
		}
	spells::BattleCast cast(&state, hero, spells::Mode::HERO, candidate.spell);
	cast.setOvercharge(candidate.spellOvercharge); cast.setMassSlow(candidate.spellMassSlow);
	cast.setSelectiveDispel(candidate.spellSelectiveDispel);
	cast.setCureAffliction(candidate.spellCureAffliction); cast.setCurePhysicalAffliction(candidate.spellCurePhysicalAffliction);
	cast.setShadowGiftSacrificePercent(candidate.spellShadowGiftSacrificePercent);
	cast.setPurifyChoices(candidate.spellPurifyChoices);
	const auto mechanics = cast.mechanicsForTarget(target);
	if(!mechanics || (mechanics->isNewHorizonsStormOfDaggers()
		&& !mechanics->setStormOfDaggersTargetCount(static_cast<int32_t>(target.size())))
		|| !mechanics->canBeCastAt(target)) return std::nullopt;
	return mechanics->getTargetAwareManaCost(target);
}
}
