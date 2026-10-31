/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CreatureBurnOwnedProjection.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/OwnedUnitBonusFrame.h"
#include "../../lib/battle/OverwhelmingFormulaState.h"
#include "../../lib/battle/GraveDustBodyCleanup.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/spells/NewHorizonsOverwhelmingFormula.h"
#include <array>
#include <vcmi/ServerCallback.h>
#include <stdexcept>
namespace creatureBurnAI
{
namespace
{
BattleStackAttacked preparedHit(const std::shared_ptr<const battle::OwnedUnitBonusFrame> & frame,
	uint32_t source, int64_t damage, vstd::RNG & representative, bool echo)
{
	// The fixed private owner is retained until its nested preparation has emitted plain state.
	auto scratch = std::make_shared<battle::OwnedUnitBonusFrame::Scratch>(frame->scratch());
	auto injury = std::shared_ptr<battle::CUnitState>(scratch, &scratch->unit());
	BattleStackAttacked hit;
	hit.attackerID = source; hit.stackAttacked = frame->unit().unitId(); hit.damageAmount = damage;
	if(echo)
	{
		hit.flags = BattleStackAttacked::SPELL_EFFECT;
		hit.spellID = SpellID(SpellID::decode(std::string(newHorizonsSoulChain::SPELL_ID)));
	}
	CStack::prepareAttacked(hit, representative, injury, false, false,
		echo ? battle::DamageProvenance::SPELL : battle::DamageProvenance::OTHER);
	return hit;
}
void applyHit(HypotheticBattle & battle, const BattleStackAttacked & hit)
{
	StacksInjured packet; packet.battleID = battle.getBattleID(); packet.stacks.push_back(hit);
	battle.getServerCallback()->apply(packet);
}
}
struct CapturedTick::Data
{
	std::weak_ptr<HypotheticBattle> authority;
	std::shared_ptr<const battle::OwnedUnitBonusFrame> original, basis;
	newHorizonsCreatureBurn::IdentityFacts identities;
	newHorizonsCreatureBurn::Tick tick;
	std::array<int, 3> fireBasis;
	size_t marker = 0;
};
namespace
{
std::array<int, 3> fireBasis(const CBattleInfoCallback & battle, const battle::Unit & target)
{
	// These are exactly the callback inputs in canonical adjustFireDamage; unit inputs live in Frame.
	return {battle.battleUsesNewHorizonsMultiplicativeMDR(),
		battle.battleGetHoldTheLineMagicalReductionBasisPoints(&target),
		battle.battleGetPerkMagicalReductionBasisPoints(&target)};
}
void validateFrame(const std::shared_ptr<const battle::OwnedUnitBonusFrame> & current,
	const std::shared_ptr<const battle::OwnedUnitBonusFrame> & original,
	const std::shared_ptr<const battle::OwnedUnitBonusFrame> & basis)
{
	if(!current || current != original || !current->hasSameQueryState(*basis))
		throw std::invalid_argument("Foreign or stale owned burn frame");
}
CSelector burnSelector(const newHorizonsCreatureBurn::IdentityFacts & identities)
{
	return [identities](const Bonus * bonus) { return newHorizonsCreatureBurn::isBurnCaptured(bonus, identities); };
}
bool sameMarker(const Bonus & left, const Bonus & right)
{
	// Both records first passed the canonical strict shape validator. Compare variable mechanical facts only.
	return left.sid == right.sid && left.val == right.val
		&& left.parameters->toNumber() == right.parameters->toNumber();
}
}
CapturedTick captureOwnedTick(const std::shared_ptr<HypotheticBattle> & authority,
	const std::shared_ptr<const battle::OwnedUnitBonusFrame> & frame, size_t markerHandle,
	BattleUnitTurnReason reason, const newHorizonsCreatureBurn::IdentityFacts & identities)
{
	if(!authority || !frame) throw std::invalid_argument("Burn requires its actual branch and owned current frame");
	auto & battle = *authority;
	const auto * current = battle.battleGetUnitByID(frame->unit().unitId());
	if(!current || current->acquireState()->save() != frame->save()) throw std::invalid_argument("Stale burn recipient");
	const auto selector = burnSelector(identities);
	const auto markers = frame->unit().getUnstackedBonuses(selector);
	const auto liveMarkers = current->getUnstackedBonuses(selector);
	if(markers->size() != 1 || liveMarkers->size() != 1 || !sameMarker(*markers->front(), *liveMarkers->front()))
		throw std::invalid_argument("Burn requires the exact owned current marker");
	auto data = std::make_shared<CapturedTick::Data>();
	data->authority = authority; data->original = frame; data->basis = frame->fork();
	validateFrame(frame, data->original, data->basis); // Reject externally mutable peer recipes before preparation.
	data->identities = identities; data->marker = markerHandle;
	auto consumed = frame->fork(); consumed->removeRecords({markerHandle});
	if(!consumed->unit().getUnstackedBonuses(selector)->empty())
		throw std::invalid_argument("Burn raw handle does not consume the owned marker");
	data->fireBasis = fireBasis(battle, frame->unit());
	data->tick = newHorizonsCreatureBurn::activationTickCaptured(battle, frame->unit(), reason, identities);
	return CapturedTick(std::move(data));
}
OwnedTick prepareOwnedCapturedTick(const std::shared_ptr<const battle::OwnedUnitBonusFrame> & frame,
	const CapturedTick & captured, vstd::RNG & representative)
{
	const auto & data = *captured.data;
	validateFrame(frame, data.original, data.basis); // All validation precedes representative RNG consumption.
	auto consumed = frame->fork(); consumed->removeRecords({data.marker});
	std::optional<BattleStackAttacked> hit;
	if(data.tick.damage > 0) hit = preparedHit(consumed, data.tick.source, data.tick.damage, representative, false);
	return OwnedTick(captured, std::move(hit));
}
std::optional<PendingEcho> applyOwnedTick(HypotheticBattle & battle,
	const std::shared_ptr<const battle::OwnedUnitBonusFrame> & frame, const OwnedTick & prepared)
{
	const auto & data = *prepared.captured.data;
	validateFrame(frame, data.original, data.basis);
	const auto authority = data.authority.lock();
	if(!authority || authority.get() != &battle) throw std::invalid_argument("Foreign or retired burn branch");
	const auto * current = battle.battleGetUnitByID(frame->unit().unitId());
	if(!current || current->acquireState()->save() != data.basis->save() || fireBasis(battle, *current) != data.fireBasis)
		throw std::invalid_argument("Stale burn recipient or external Fire basis");
	const auto markers = current->getUnstackedBonuses(burnSelector(data.identities));
	const auto basisMarkers = data.basis->unit().getUnstackedBonuses(burnSelector(data.identities));
	if(markers->size() != 1 || !sameMarker(*markers->front(), *basisMarkers->front()))
		throw std::invalid_argument("Stale owned burn marker");
	if(data.tick.remove.empty()) return std::nullopt;
	if(prepared.hit) newHorizonsGraveDust::validateBodyChanges(battle, {*prepared.hit});
	const auto unit = battle.getForUpdate(frame->unit().unitId());
	unit->removeUnitBonus(data.tick.remove); // Consume BEFORE injury and reaction capture.
	std::optional<PendingEcho> echo;
	if(prepared.hit && prepared.hit->damageAmount > 0 && newHorizonsSoulChain::isEnabled(battle.getMagicRules()))
		if(const auto link = newHorizonsSoulChain::linkFor(unit.get()); link
			&& link->primaryUnitId != unit->unitId() && link->casterSide != unit->unitSide())
			echo = PendingEcho{*link, unit->unitSide(), prepared.hit->damageAmount};
	if(prepared.hit) applyHit(battle, *prepared.hit);
	return echo;
}
struct OwnedEcho::Data
{
	std::weak_ptr<HypotheticBattle> authority;
	std::shared_ptr<const battle::OwnedUnitBonusFrame> original, basis;
	PendingEcho echo;
	BattleStackAttacked hit;
	OverwhelmingFormulaState attackerFormula, defenderFormula;
	std::array<int, 3> fireBasis;
	int64_t adjustedDamage = 0;
};
const BattleStackAttacked & OwnedEcho::injury() const
{
	if(!data) throw std::invalid_argument("Empty echo receipt");
	return data->hit;
}
std::optional<OwnedEcho> prepareOwnedEcho(const std::shared_ptr<HypotheticBattle> & authority,
	const PendingEcho & echo, const std::shared_ptr<const battle::OwnedUnitBonusFrame> & primary,
	vstd::RNG & representative)
{
	if(!authority) throw std::invalid_argument("Echo requires its actual live branch");
	auto & battle = *authority;
	if((echo.link.casterSide != BattleSide::ATTACKER && echo.link.casterSide != BattleSide::DEFENDER)
		|| (echo.victimSide != BattleSide::ATTACKER && echo.victimSide != BattleSide::DEFENDER)
		|| echo.link.casterSide == echo.victimSide || echo.actualDamage < 0)
		throw std::invalid_argument("Invalid captured echo relationship");
	if(!primary) return std::nullopt;
	if(primary->unit().unitId() != echo.link.primaryUnitId || primary->unit().unitSide() != echo.victimSide)
		throw std::invalid_argument("Foreign echo primary frame");
	if(!primary->unit().alive()) return std::nullopt;
	auto data = std::make_shared<OwnedEcho::Data>();
	data->authority = authority; data->original = primary; data->basis = primary->fork();
	validateFrame(primary, data->original, data->basis);
	const auto * current = battle.battleGetUnitByID(echo.link.primaryUnitId);
	if(!current || current->acquireState()->save() != primary->save())
		throw std::invalid_argument("Stale echo primary");
	data->echo = echo;
	data->adjustedDamage = newHorizonsSoulChain::adjustedEchoDamage(battle, echo.link.casterSide,
		&primary->unit(), echo.actualDamage, echo.link.echoBasisPoints, &echo.link.mdrPenetration,
		echo.link.damageMultiplierPercent);
	if(data->adjustedDamage <= 0) return std::nullopt;
	data->attackerFormula = battle.getOverwhelmingFormulaState(BattleSide::ATTACKER);
	data->defenderFormula = battle.getOverwhelmingFormulaState(BattleSide::DEFENDER);
	data->fireBasis = fireBasis(battle, primary->unit());
	uint32_t source = primary->unit().unitId();
	for(const auto * candidate : battle.battleGetAllStacks(true))
		if(candidate && candidate->unitSide() == echo.link.casterSide)
		{
			source = candidate->unitId(); break; // Same bounded native attribution lookup.
		}
	data->hit = preparedHit(primary, source, data->adjustedDamage, representative, true);
	return OwnedEcho(std::move(data));
}
void applyOwnedEcho(HypotheticBattle & battle,
	const std::shared_ptr<const battle::OwnedUnitBonusFrame> & primary, const OwnedEcho & prepared)
{
	if(!prepared.data) throw std::invalid_argument("Empty echo receipt");
	const auto & data = *prepared.data;
	validateFrame(primary, data.original, data.basis);
	const auto authority = data.authority.lock();
	if(!authority || authority.get() != &battle) throw std::invalid_argument("Foreign or retired echo branch");
	const auto * current = battle.battleGetUnitByID(data.hit.stackAttacked);
	if(!current || current->acquireState()->save() != data.basis->save()
		|| fireBasis(battle, *current) != data.fireBasis
		|| battle.getOverwhelmingFormulaState(BattleSide::ATTACKER) != data.attackerFormula
		|| battle.getOverwhelmingFormulaState(BattleSide::DEFENDER) != data.defenderFormula
		|| !newHorizonsSoulChain::isEchoHit(data.hit))
		throw std::invalid_argument("Stale echo state");
	const auto & echo = data.echo;
	if(newHorizonsSoulChain::adjustedEchoDamage(battle, echo.link.casterSide, current,
		echo.actualDamage, echo.link.echoBasisPoints, &echo.link.mdrPenetration,
		echo.link.damageMultiplierPercent) != data.adjustedDamage)
		throw std::invalid_argument("Stale echo damage basis");
	newHorizonsGraveDust::validateBodyChanges(battle, {data.hit});
	if(auto claim = spells::overwhelmingFormulaClaim(battle, data.hit.spellID.toSpell(),
		echo.link.mdrPenetration, {data.hit}))
		battle.getServerCallback()->apply(*claim);
	applyHit(battle, data.hit); // Canonical claim precedes injury; explicit echo tag prevents recursion.
}
}
