/*
 * HypotheticOrderActions.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/HeroOrderEffects.h"
#include "../../lib/battle/OrderActionTransition.h"
#include "../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

bool HypotheticBattle::projectAcceptedHeroOrder(BattleSide side, const ProjectedOrderAllowance & prepared)
{
	return projectAcceptedHeroOrder(side, HeroCommand::NONE, {}, prepared);
}

bool HypotheticBattle::projectAcceptedHeroOrder(BattleSide side, HeroCommand command,
	const std::vector<uint32_t> & commandTargets, const ProjectedOrderAllowance & prepared)
{
	if(command == HeroCommand::NONE)
	{
		if((side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
			&& (begunProjectedOrderEffects.at(side) || begunProjectedLegacyOrders.at(side)))
			return false; // A bound canonical action cannot be downgraded to accounting only.
		return commitProjectedHeroOrder(side, nullptr, prepared);
	}
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return false;
	if(!heroCommands::isCanonicalRules(getHeroCommandRules()))
	{
		const auto & legacy = begunProjectedLegacyOrders.at(side);
		return legacy && legacy->owner == this && legacy->command == command
			&& legacy->targets == commandTargets
			&& commitProjectedHeroOrder(side, nullptr, prepared, command);
	}
	const auto & bound = begunProjectedOrderEffects.at(side);
	return bound && bound->owner == this && bound->effects.order.command == command
		&& bound->targets == commandTargets && projectAcceptedHeroOrder(side, bound->effects, prepared);
}

std::optional<heroCommands::PreparedOrderEffects> HypotheticBattle::beginProjectedHeroOrder(
	BattleSide side, HeroCommand command, const std::vector<uint32_t> & targets,
	const ProjectedOrderAllowance & prepared)
{
	if(!isCurrentPreparedOrderAction(side, prepared, false))
		return {};
	auto effects = heroCommands::prepareOrderEffects(*this, side, command, targets);
	if(!effects)
		return {};
	heroCommands::finalizeRevisedOrderSnapshot(effects->order, getBattle()->getMagicRules());
	// Canonical preparation validates both shapes before action-boundary cleanup.
	// Bind an owned copy, not the openly mutable value returned to the caller.
	auto bound = BoundProjectedOrderEffects{this, *effects, targets};
	if(!beginProjectedHeroAction(side, prepared))
		return {};
	begunProjectedOrderEffects.at(side) = std::move(bound);
	return effects;
}

bool HypotheticBattle::projectAcceptedHeroOrder(BattleSide side,
	const heroCommands::PreparedOrderEffects & effects, const ProjectedOrderAllowance & prepared)
{
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| !isCurrentPreparedOrderAction(side, prepared, true))
		return false;
	const auto & bound = begunProjectedOrderEffects.at(side);
	if(!bound || bound->owner != this || bound->effects != effects)
		return false;
	return commitProjectedHeroOrder(side, &effects, prepared);
}

bool HypotheticBattle::commitProjectedHeroOrder(BattleSide side,
	const heroCommands::PreparedOrderEffects * effects, const ProjectedOrderAllowance & prepared,
	HeroCommand legacyCommand)
{
	const auto command = effects ? effects->order.command : legacyCommand;
	const auto & action = prepared.action;
	if(action.receipt.action != HeroActionAllowanceState::ActionKind::ORDER
		|| !action.isHeroAction() || !isCurrentPreparedOrderAction(side, prepared, true))
		return false;
	const bool finiteCommand = newHorizonsCommand::usesRules(getHeroCommandRules());
	auto paidResources = getCommandResources(side);
	if(finiteCommand && (command == HeroCommand::NONE || !paidResources.spend()))
		return false;
	if(command != HeroCommand::NONE && !heroCommands::isActive(command))
		return false;
	const auto mandateStatus = battleGetDivineMandateStatus(side);
	if(legacyCommand != HeroCommand::NONE)
		applyProjectedLegacyHeroOrder(side);
	const auto * enchantedOrder = effects ? &effects->order : nullptr;
	std::vector<uint32_t> sharedPurposeOverlap;
	if(action.typedLedger)
	{
		auto nextLedger = prepared.allowancesAfter;
		const auto recipients = enchantedOrder
			&& newHorizonsDivineMandate::needsRecipientCapture(getSideHero(side))
			? newHorizonsDivineMandate::sharedPurposeOrderRecipients(*this, side, *enchantedOrder)
			: std::vector<uint32_t>{};
		sharedPurposeOverlap = DivineMandateTransition::applyAcceptedAction(nextLedger, action.receipt,
			projectedRound, false, mandateStatus.maximumPairs, recipients);
		heroActionAllowances.at(side) = std::move(nextLedger);
	}

	if(effects)
	{
		setHeroOrderState(side, effects->order);
		if(effects->focusFire)
			setFocusFireState(side, *effects->focusFire);
	}
	newHorizonsDivineMandate::applySharedPurpose(*this, *this, side, sharedPurposeOverlap);
	if(effects)
		heroCommands::applyIssuedOrderGrants(*this, heroCommands::prepareIssuedOrderGrants(*this, side, effects->order));
	if(finiteCommand)
		commandResources.at(side) = paidResources;
	finishProjectedHeroAction(side, prepared);
	return true;
}
