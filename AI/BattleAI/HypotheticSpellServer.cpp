/*
	* HypotheticSpellServer.cpp, part of VCMI engine
	*
	* Authors: listed in file AUTHORS in main folder
	*
	* License: GNU General Public License v2.0 or later
	* Full text of license available in license.txt file, in main folder
	*
	*/
#include "StdInc.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/spells/AutomaticHeroCast.h"
#include "../../lib/ScopeGuard.h"
#include "ProjectedInjuryLifecycle.h"
#include "../../lib/battle/GraveDustEffectChanges.h"
#include "../../lib/battle/GraveDustBodyCleanup.h"
#include "StackWithBonuses.h"
#include "PhysicalStrikeProjection.h"

#include "../../lib/battle/NewHorizonsPlague.h"
#include "../../lib/constants/NumericConstants.h"
#include "../../lib/gameState/GameStatePackVisitor.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "HypotheticBattle.h"
#include "HypotheticBattleTypes.h"

HypotheticBattle::HypotheticServerCallback::HypotheticServerCallback(HypotheticBattle * owner_)
	:owner(owner_)
{
}

void HypotheticBattle::rejectProjectedSpellEvaluation(BattleSide side) noexcept
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
	{
		spellPaymentScopes.invalidate();
		return;
	}
	const auto & begun = begunProjectedSpellAllowances.at(side);
	if(begun) rejectedSpellPaymentEpochs.at(side) = begun->action.epoch;
	evaluatedSpellPayments.at(side).reset();
}
uint64_t HypotheticBattle::HypotheticServerCallback::beginSpellEvaluation(SpellEvaluationKind kind,
	BattleSide side, const SpellID & spell)
{
	std::optional<uint64_t> epoch;
	try
	{
		if(kind == SpellEvaluationKind::MANUAL_HERO)
		{
			const auto & begun = owner->begunProjectedSpellAllowances.at(side);
			if(begun)
			{
				epoch = begun->action.epoch;
				owner->nativeSpellAttemptEpochs.at(side) = epoch;
				if(begun->spell != spell || begun->action.epoch != owner->projectedActionEpochs.at(side)
					|| begun->action.receipt.round != owner->projectedRound
					|| owner->rejectedSpellPaymentEpochs.at(side) == epoch)
					throw std::invalid_argument("Native evaluation does not match an admissible begun action");
			}
		}
		return owner->spellPaymentScopes.begin(kind, side, spell, epoch);
	}
	catch(...)
	{
		if(kind == SpellEvaluationKind::MANUAL_HERO) owner->rejectProjectedSpellEvaluation(side);
		throw;
	}
}
void HypotheticBattle::HypotheticServerCallback::admitSpellEvaluation(uint64_t token, int32_t cost, bool admitted)
{
	const auto kind = owner->spellPaymentScopes.currentKind();
	const auto side = owner->spellPaymentScopes.currentSide();
	const auto manual = owner->spellPaymentScopes.activeManualAction();
	try
	{
		if(manual)
		{
			const auto & prior = owner->evaluatedSpellPayments.at(side);
			if(owner->rejectedSpellPaymentEpochs.at(side) == manual->second
				|| (prior && (prior->evaluated || prior->exactCost != cost || prior->actionEpoch != manual->second)))
				throw std::invalid_argument("Native evaluation cost does not match its preflight action");
		}
		const auto * state = kind == SpellEvaluationKind::OTHER ? nullptr : owner->forecastSide(side).get();
		owner->spellPaymentScopes.admit(token, state, cost, admitted);
	}
	catch(...)
	{
		if(kind == SpellEvaluationKind::MANUAL_HERO) owner->rejectProjectedSpellEvaluation(side);
		throw;
	}
}
void HypotheticBattle::HypotheticServerCallback::allowSpellEvaluationDelegation(uint64_t token)
{
	owner->spellPaymentScopes.allowDelegation(token);
}
void HypotheticBattle::HypotheticServerCallback::endSpellEvaluation(uint64_t token, bool completed) noexcept
{
	const auto manual = owner->spellPaymentScopes.activeManualAction();
	const bool delegated = owner->spellPaymentScopes.completedDelegation();
	auto ticket = owner->spellPaymentScopes.finish(token, completed);
	if(manual && (!completed || !owner->spellPaymentScopes.healthy()))
		owner->rejectProjectedSpellEvaluation(manual->first);
	if(!ticket)
	{
		// An unadmitted outer concentration frame may have delegated exactly one
		// completed inner evaluation. Every other incomplete attempt is inert.
		if(manual)
		{
			const auto & prior = owner->evaluatedSpellPayments.at(manual->first);
			if(!completed || !delegated || !prior || !prior->evaluated || prior->actionEpoch != manual->second)
				owner->rejectProjectedSpellEvaluation(manual->first);
		}
		return;
	}
	const auto & begun = owner->begunProjectedSpellAllowances.at(ticket->side);
	if(!begun || begun->spell != ticket->spell || begun->action.epoch != ticket->actionEpoch
		|| owner->rejectedSpellPaymentEpochs.at(ticket->side) == ticket->actionEpoch)
	{
		owner->rejectProjectedSpellEvaluation(ticket->side);
		return;
	}
	owner->evaluatedSpellPayments.at(ticket->side) = std::move(ticket);
}

void HypotheticBattle::HypotheticServerCallback::complain(const std::string & problem)
{
	logAi->error(problem);
}

bool HypotheticBattle::HypotheticServerCallback::describeChanges() const
{
	return false;
}

void HypotheticBattle::HypotheticServerCallback::recordCrossSchoolFormulaCast(
	BattleSide side, const newHorizonsCrossSchoolFormula::Receipt & receipt)
{
	if(newHorizonsCrossSchoolFormula::acceptedReceipt(*owner, side, receipt.after.spell) != receipt)
		throw std::runtime_error("Stale detached Cross-School Formula receipt");
	owner->setCrossSchoolFormulaState(side, receipt.after);
}

void HypotheticBattle::HypotheticServerCallback::recordCompletedHeroSpellCast(BattleSide side)
{
	recordCompletedHeroSpellCast(side, 0);
}

void HypotheticBattle::HypotheticServerCallback::recordCompletedHeroSpellCast(BattleSide side, int32_t spellLevel)
{
	if(side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
	{
		const auto receipt = forecast::SideTransitions::recordCompletion(owner->forecastSide(side), spellLevel, owner->searchBudget().get());
		if(!receipt || !owner->commitForecastSide(side, *receipt)) throw std::invalid_argument("Unknown projected cast completion owner");
	}
}

void HypotheticBattle::HypotheticServerCallback::recordCompletedAutomaticHeroSpellCast(
	BattleSide side, const SpellID & spell, const spells::AutomaticHeroCastReceipt & receipt)
{
	const auto * hero = owner->battleGetFightingHero(side);
	if(!spells::automaticHeroCastAllowed(*owner, hero, spell.toSpell(), receipt))
		throw std::runtime_error("Invalid automatic hero cast projection receipt");
	const auto completed = forecast::SideTransitions::recordCompletion(owner->forecastSide(side),
		owner->battleGetSpellLevel(spell), owner->searchBudget().get(), spell);
	if(!completed) throw std::invalid_argument("Unsupported projected automatic cast owner");
	if(receipt.kind == spells::AutomaticHeroCastKind::DIVINE_MANDATE)
		owner->setPassiveDivineMandateUses(side, receipt.mandateUseOrdinal);
	if(!owner->commitForecastSide(side, *completed))
		throw std::logic_error("Stale synchronous automatic cast receipt");
}

vstd::RNG * HypotheticBattle::HypotheticServerCallback::getRNG()
{
	return &rngStub;
}

bool HypotheticBattle::HypotheticServerCallback::rollCombatAbility(const IBattleInfoCallback &, const battle::Unit &, int percentageChance)
{
	// same averaging the RNG stub does - the hypothetical battle must stay deterministic
	return percentageChance >= 50;
}

bool HypotheticBattle::HypotheticServerCallback::rollHostileCombatAbility(const IBattleInfoCallback & battle,
	const battle::Unit & actor, const battle::Unit & recipient, int percentageChance)
{
	const int basisPoints = actor.favorableCreatureAbilityChanceBasisPoints(percentageChance);
	int chance = basisPoints / 100;
	if(basisPoints % 100 && rngStub.nextInt(0, 99) < basisPoints % 100)
		++chance;
	const auto draw = [&]() { return rollCombatAbility(battle, actor, chance); };
	const auto actorSide = owner->playerToSide(owner->battleGetOwner(&actor));
	const auto recipientSide = owner->playerToSide(owner->battleGetOwner(&recipient));
	const bool hostile = (actorSide == BattleSide::ATTACKER || actorSide == BattleSide::DEFENDER)
		&& (recipientSide == BattleSide::ATTACKER || recipientSide == BattleSide::DEFENDER)
		&& actorSide != recipientSide && actor.alive() && recipient.alive()
		&& recipient.isValidTarget(false) && !recipient.isInvincible();
	if(!hostile)
		return draw();
	return resolveAdverseCombatRoll(battle.getBattle()->getBattleID(), recipientSide,
		chance > 0 && chance < 100, true, draw);
}

bool HypotheticBattle::HypotheticServerCallback::resolveAdverseCombatRoll(const BattleID & battleID,
	BattleSide side, bool stochastic, bool adverseOnTrue, const std::function<bool()> & draw)
{
	if(battleID != owner->getBattleID())
		throw std::runtime_error("Adverse combat projection refers to another battle");
	const bool firstResult = draw();
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return firstResult;
	auto state = owner->getAdverseCombatRerollState(side);
	if(!state.consume(stochastic, firstResult == adverseOnTrue))
		return firstResult;

	BattleAdverseRerollStateChanged packet;
	packet.battleID = battleID;
	packet.side = side;
	packet.state = state;
	BattleStatePackVisitor visitor(*owner);
	packet.visit(visitor);
	return draw();
}

void HypotheticBattle::HypotheticServerCallback::apply(CPackForClient & pack)
{
	if(const auto * mana = dynamic_cast<const SetMana *>(&pack);
		mana && (mana->operation == SetMana::Operation::RESTORE_NORMAL
			|| mana->operation == SetMana::Operation::REFUND_HERO_LUCK))
	{
		owner->restoreProjectedNormalMana(*mana);
		return;
	}
	if(dynamic_cast<SetSpellResponseState *>(&pack)
		|| dynamic_cast<SetOverwhelmingFormulaState *>(&pack))
	{
		BattleStatePackVisitor visitor(*owner);
		pack.visit(visitor);
		return;
	}
	logAi->error("Package of type %s is not allowed in battle evaluation", typeid(pack).name());
}

void HypotheticBattle::HypotheticServerCallback::apply(BattleLogMessage & pack)
{
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
}

void HypotheticBattle::HypotheticServerCallback::apply(BattleStackMoved & pack)
{
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
}

void HypotheticBattle::HypotheticServerCallback::apply(BattleUnitsChanged & pack)
{
	const auto destroyed = PhysicalStrikeProjection::captureDamagePacket(*owner, std::span<const UnitChanges>(pack.changedStacks));
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
	PhysicalStrikeProjection::completeDamagePacket(*owner, destroyed);
}

void HypotheticBattle::HypotheticServerCallback::apply(SetStackEffect & pack)
{
	if(owner->usesOwnedFrameQueries()) { owner->applyOwnedEffectChanges(pack); return; }
	owner->preflightEffectChanges(pack);
	const auto * previousPacket = owner->ordinaryValidatedPacket;
	const auto restorePacket = vstd::makeScopeGuard([&]() { owner->ordinaryValidatedPacket = previousPacket; });
	owner->ordinaryValidatedPacket = &pack;
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
}

void HypotheticBattle::HypotheticServerCallback::apply(StacksInjured & pack)
{
	newHorizonsGraveDust::validateBodyChanges(*owner, pack.stacks);
	const auto destroyed = PhysicalStrikeProjection::captureDamagePacket(*owner, std::span<const BattleStackAttacked>(pack.stacks));
	std::vector<ProjectedPlagueHit> plagueHits;
	static const SpellID plagueSpell(SpellID::decode(std::string(newHorizonsPlague::SPELL_ID)));
	for(const auto & hit : pack.stacks)
	{
		if(!(hit.flags & BattleStackAttacked::SPELL_EFFECT) || hit.spellID != plagueSpell || hit.damageAmount <= 0)
			continue;
		const auto target = owner->getForUpdate(hit.stackAttacked);
		if(!target || !target->alive())
			continue;
		auto snapshot = std::make_shared<StackWithBonuses>(owner, target.get());
		snapshot->freezeDamageScoringBonusSnapshot();
		plagueHits.push_back({snapshot, owner->battleGetOwner(target.get()), hit.damageAmount});
	}


	projectedInjuryLifecycle::before(*owner, pack.stacks);
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
	projectedInjuryLifecycle::after(*owner, pack.stacks);
	PhysicalStrikeProjection::completeDamagePacket(*owner, destroyed);
	// Publish only after successful state application.
	owner->projectedPlagueHits.insert(owner->projectedPlagueHits.end(), plagueHits.begin(), plagueHits.end());
}

void HypotheticBattle::HypotheticServerCallback::apply(BattleObstaclesChanged & pack)
{
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
}

void HypotheticBattle::HypotheticServerCallback::apply(CatapultAttack & pack)
{
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
}

HypotheticBattle::HypotheticEnvironment::HypotheticEnvironment(HypotheticBattle * owner_, const Environment * upperEnvironment)
	: owner(owner_),
	env(upperEnvironment)
{

}

const Services * HypotheticBattle::HypotheticEnvironment::services() const
{
	if(owner->usesOwnedFrameQueries()) throw std::logic_error("Owned recipient cannot request native services");
	return env->services();
}

const Environment::BattleCb * HypotheticBattle::HypotheticEnvironment::battle(const BattleID & battleID) const
{
	if(owner->usesOwnedFrameQueries() && battleID != owner->getBattleID())
		throw std::invalid_argument("Foreign owned battle environment lookup");
	assert(battleID == owner->getBattleID());
	return owner;
}

const Environment::GameCb * HypotheticBattle::HypotheticEnvironment::game() const
{
	if(owner->usesOwnedFrameQueries()) throw std::logic_error("Owned recipient cannot request native game state");
	return env->game();
}
