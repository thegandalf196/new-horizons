/*
 * HypotheticHeroActions.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "PossibleSpellcast.h"
#include "ForecastSpellPayment.h"
#include "OwnedStackQuerySource.h"
#include "../../lib/battle/OwnedFrameBranchQueries.h"
#include "../../lib/battle/OrderActionTransition.h"
#include "../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"

namespace
{
bool isDivineMandateLightSpell(const CBattleInfoCallback & battle, SpellID spell)
{
	const auto schools = battle.battleGetSpellSchools(spell);
	return std::any_of(schools.begin(), schools.end(), [](const SpellSchool & school)
	{
		return school.serializationKey() == "new-horizons:light";
	});
}

}

std::optional<HypotheticBattle::ProjectedSpellAllowance> HypotheticBattle::prepareHeroSpellAllowance(
	BattleSide side, SpellID spell) const
{
	const auto & state = forecastSide(side);
	if(!state->hasVisibleHero() || !state->ledger || !state->mana) return {};
	const auto & ledger = state->ledger->allowances;
	if(!state->knowsSpell(spell)) return {};
	if(ledger.currentRound < 0)
	{
		// Legacy battles have no typed ledger. Preserve their historical distinction:
		// only an ordinary cast spends the flexible Hero Action.
		ProjectedActionReceipt action;
		action.side = side;
		action.typedLedger = false;
		action.epoch = projectedActionEpochs.at(side);
		action.receipt = {0, HeroActionAllowanceState::ActionKind::SPELL,
			HeroActionAllowanceState::AllowanceKind::HERO,
			HeroActionAllowanceState::GrantSource::OTHER,
			projectedRound};
		ProjectedSpellAllowance prepared;
		prepared.action = action;
		prepared.allowancesBefore = ledger;
		prepared.allowancesAfter = ledger;
		prepared.spell = spell;
		return prepared;
	}

	try
	{
		auto nextLedger = ledger;
		if(nextLedger.currentRound != projectedRound)
			return {};
		const auto candidateSelection = battleGetSpellActionAllowance(side, spell);
		if(!candidateSelection)
			return {};
		const auto selectedGrantId = candidateSelection->grantId;
		const auto grantFilter = [selectedGrantId](const HeroActionAllowanceState::Grant & grant)
		{
			return grant.id == selectedGrantId;
		};
		const auto selection = nextLedger.eligibleAllowance(
			HeroActionAllowanceState::ActionKind::SPELL, projectedRound, grantFilter);
		if(!selection || selection->grantId != candidateSelection->grantId)
			return {};
		const auto transition = nextLedger.consumeAllowance(selection->grantId,
			HeroActionAllowanceState::ActionKind::SPELL, projectedRound, grantFilter);
		if(!transition)
			return {};

		ProjectedSpellAllowance prepared;
		prepared.action = {side, *transition, true, projectedActionEpochs.at(side)};
		prepared.allowancesBefore = ledger;
		prepared.allowancesAfter = std::move(nextLedger);
		prepared.spell = spell;
		return prepared;
	}
	catch(const std::exception &)
	{
		// Malformed or stale candidate state is rejected before any projected effect
		// (including action-boundary cleanup) can touch this clone.
		return {};
	}
}

std::optional<HypotheticBattle::ProjectedOrderAllowance> HypotheticBattle::prepareHeroOrderAllowance(
	BattleSide side) const
{
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| !newHorizonsCommand::available(*this, side))
		return {};
	const bool finite = newHorizonsCommand::usesFiniteResources(getHeroCommandRules());
	if(finite && !hasCommandResourceObservation(side)) return {};
	const auto resources = finite ? getCommandResources(side) : newHorizonsCommand::ResourceState{};
	const auto & state = forecastSide(side);
	if(!state->hasVisibleHero() || !state->ledger || !state->mana) return {};
	const auto & ledger = state->ledger->allowances;
	if(ledger.currentRound < 0)
	{
		ProjectedActionReceipt action;
		action.side = side;
		action.typedLedger = false;
		action.epoch = projectedActionEpochs.at(side);
		action.receipt = {0, HeroActionAllowanceState::ActionKind::ORDER,
			HeroActionAllowanceState::AllowanceKind::HERO,
			HeroActionAllowanceState::GrantSource::OTHER, projectedRound};
		return ProjectedOrderAllowance{action, ledger, ledger, resources};
	}

	try
	{
		auto nextLedger = ledger;
		if(nextLedger.currentRound != projectedRound)
			return {};
		const auto selection = battleGetOrderActionAllowance(side);
		if(!selection)
			return {};
		const auto receipt = orderActionTransition::consume(nextLedger, *selection, projectedRound);
		if(!receipt)
			return {};
		return ProjectedOrderAllowance{{side, *receipt, true, projectedActionEpochs.at(side)},
			ledger, std::move(nextLedger), resources};
	}
	catch(const std::exception &)
	{
		return {};
	}
}

bool HypotheticBattle::isCurrentPreparedSpellAction(BattleSide side,
	const ProjectedSpellAllowance & prepared, bool requireBegun) const
{
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| prepared.action.side != side
		|| prepared.action.epoch != projectedActionEpochs.at(side)
		|| prepared.action.receipt.round != projectedRound)
		return false;
	const auto & begunSpell = begunProjectedSpellAllowances.at(side);
	const auto & begunOrder = begunProjectedOrderAllowances.at(side);
	if((requireBegun && (!begunSpell || *begunSpell != prepared || begunOrder.has_value()))
		|| (!requireBegun && (begunSpell.has_value() || begunOrder.has_value())))
		return false;
	const auto current = prepareHeroSpellAllowance(side, prepared.spell);
	return current && *current == prepared;
}

bool HypotheticBattle::isCurrentPreparedOrderAction(BattleSide side,
	const ProjectedOrderAllowance & prepared, bool requireBegun) const
{
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| prepared.action.side != side
		|| prepared.action.epoch != projectedActionEpochs.at(side)
		|| prepared.action.receipt.round != projectedRound)
		return false;
	const auto & begunSpell = begunProjectedSpellAllowances.at(side);
	const auto & begunOrder = begunProjectedOrderAllowances.at(side);
	if((requireBegun && (!begunOrder || *begunOrder != prepared || begunSpell.has_value()))
		|| (!requireBegun && (begunSpell.has_value() || begunOrder.has_value())))
		return false;
	const auto current = prepareHeroOrderAllowance(side);
	return current && *current == prepared;
}

bool HypotheticBattle::beginProjectedHeroAction(BattleSide side, const ProjectedSpellAllowance & prepared)
{
	if(!isCurrentPreparedSpellAction(side, prepared, false))
		return false;
	begunProjectedSpellAllowances.at(side) = prepared;
	evaluatedSpellPayments.at(side).reset();
	nativeSpellAttemptEpochs.at(side).reset();
	rejectedSpellPaymentEpochs.at(side).reset();
	recordedWisdomActionEpochs.at(side).reset();
	arcaneInspirationEffects.clear();
	beginProjectedHeroAction(side, prepared.action);
	return true;
}

bool HypotheticBattle::beginProjectedHeroAction(BattleSide side, const ProjectedOrderAllowance & prepared)
{
	if(!isCurrentPreparedOrderAction(side, prepared, false))
		return false;
	begunProjectedOrderAllowances.at(side) = prepared;
	beginProjectedHeroAction(side, prepared.action);
	return true;
}

void HypotheticBattle::beginProjectedHeroAction(BattleSide side, const ProjectedActionReceipt & action)
{
	if(action.side != side || action.epoch != projectedActionEpochs.at(side))
		return;
	// The complete typed plan was revalidated and recorded before this cleanup
	// boundary; commit must present that same plan, not another same-epoch action.
	if(!action.isHeroAction())
		return;

}

void HypotheticBattle::finishProjectedHeroAction(BattleSide side, const ProjectedSpellAllowance & prepared)
{
	if(begunProjectedSpellAllowances.at(side)
		&& *begunProjectedSpellAllowances.at(side) == prepared)
	{
		begunProjectedSpellAllowances.at(side).reset();
		evaluatedSpellPayments.at(side).reset();
		recordedWisdomActionEpochs.at(side).reset();
		++projectedActionEpochs.at(side);
	}
}

void HypotheticBattle::finishProjectedHeroAction(BattleSide side, const ProjectedOrderAllowance & prepared)
{
	if(begunProjectedOrderAllowances.at(side)
		&& *begunProjectedOrderAllowances.at(side) == prepared)
	{
		begunProjectedOrderAllowances.at(side).reset();
		begunProjectedOrderEffects.at(side).reset();
		begunProjectedLegacyOrders.at(side).reset();
		++projectedActionEpochs.at(side);
	}
}

bool HypotheticBattle::projectAcceptedHeroSpell(BattleSide side, SpellID spell, uint32_t target,
	const ProjectedSpellAllowance & prepared, const std::vector<uint32_t> & affectedRecipients)
{
	const auto & action = prepared.action;
	if(action.receipt.action != HeroActionAllowanceState::ActionKind::SPELL
		|| spell != prepared.spell
		|| !isCurrentPreparedSpellAction(side, prepared, true))
		return false;
	const bool lightSpell = isDivineMandateLightSpell(*this, spell);
	const auto mandateStatus = battleGetDivineMandateStatus(side);

	const auto recipients = lightSpell
		&& newHorizonsDivineMandate::needsRecipientCapture(getSideHero(side))
		? newHorizonsDivineMandate::sharedPurposeFriendlyRecipients(*this, side, affectedRecipients)
		: std::vector<uint32_t>{};
	const auto & payment = evaluatedSpellPayments.at(side);
	if(!spellPaymentScopes.healthy() || !payment || payment->side != side || payment->spell != spell
		|| payment->actionEpoch != prepared.action.epoch
		|| rejectedSpellPaymentEpochs.at(side) == prepared.action.epoch
		|| (nativeSpellAttemptEpochs.at(side) == prepared.action.epoch && !payment->evaluated)) return false;
	const auto receipt = forecast::SideTransitions::acceptedAction(forecastSide(side), action.receipt,
		prepared.allowancesAfter, lightSpell, mandateStatus.maximumPairs, recipients,
		forecast::CastCost{spell, payment->exactCost, false}, searchBudget().get(), payment->refunds, payment->residualLifeDamage);
	if(!receipt || !commitForecastSide(side, *receipt)) return false;
	evaluatedSpellPayments.at(side).reset();
	newHorizonsDivineMandate::applySharedPurpose(*this, *this, side, receipt->sharedPurposeRecipients());

	// castEval already records ordinary casts before effects. The
	// precomputed-value paths skip it, but accept the same manual receipt here.
	// Automatic casts never satisfy this manual epoch, even for the same spell.
	if(recordedWisdomActionEpochs.at(side) != prepared.action.epoch)
	{
		serverCallback->recordCompletedWisdomSpellCast(side, spell, projectedRound, false);
		completeSyntheticInspiration(side);
	}
	setElementalAttunementMask(side, newHorizonsElementalAttunement::completedHeroCastMask(
			*this, side, spell, getElementalAttunementMask(side)));
	finishProjectedHeroAction(side, prepared);
	return true;
}


bool HypotheticBattle::projectHeroSpellAllowance(BattleSide side, SpellID spell, uint32_t target)
{
	const auto prepared = prepareHeroSpellAllowance(side, spell);
	if(!prepared)
		return false;
	PossibleSpellcast candidate; candidate.command = HeroCommand::NONE; candidate.spell = spell.toSpell();
	if(const auto * unit = battleGetUnitByID(target)) candidate.dest.emplace_back(unit);
	const auto cost = forecast::candidateSpellPaymentCost(*this, side, candidate);
	if(!cost || !beginProjectedHeroAction(side, *prepared) || !stageProjectedSpellPayment(side, spell, *cost)) return false;
	return projectAcceptedHeroSpell(side, spell, target,
		*prepared);
}

bool HypotheticBattle::projectHeroOrderAllowance(BattleSide side)
{
	const auto prepared = prepareHeroOrderAllowance(side);
	if(!prepared || !beginProjectedHeroAction(side, *prepared))
		return false;
	return projectAcceptedHeroOrder(side, *prepared);
}
