/*
 * NewHorizonsOrderIndicators.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "NewHorizonsOrderBadgeHelp.h"
#include "../battle/NewHorizonsOrderPresentation.h"
#include "../battle/NewHorizonsBattleStatus.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleInfo.h"

namespace newHorizonsOrderIndicators
{
struct OrderIndicator
{
	HeroCommand command = HeroCommand::NONE;
	std::string label;
	std::string description;
};

inline bool ordinaryOrderStack(const CStack * stack)
{
	return stack && stack->alive() && !stack->isGhost() && !stack->isTurret()
		&& !stack->hasBonusOfType(BonusType::SIEGE_WEAPON)
		&& stack->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER;
}

inline std::string orderName(HeroCommand command)
{
	switch(command)
	{
	case HeroCommand::CHARGE: return "Charge";
	case HeroCommand::HOLD_THE_LINE: return "Hold the Line";
	case HeroCommand::FOCUS_FIRE: return "Focus Fire";
	case HeroCommand::RIPOSTE: return "Riposte";
	case HeroCommand::BRACE: return "Brace";
	case HeroCommand::PROTECT: return "Protect";
	case HeroCommand::FLANK: return "Flank";
	case HeroCommand::SECOND_WIND: return "Second Wind";
	case HeroCommand::FEINT: return "Feint";
	case HeroCommand::VANGUARD: return "Vanguard";
	default: return "Order";
	}
}

inline std::vector<OrderIndicator> activeOrderIndicators(const CStack * stack)
{
	std::vector<OrderIndicator> result;
	if(!ordinaryOrderStack(stack) || !stack->getBattle())
		return result;

	// Timers on the unit can outlive the latest side snapshot while Time Stop
	// pauses an older grant. Show the native bonus, including after snapshot expiry.
	if(const auto vanguard = heroCommands::activeVanguardBonuses(stack))
	{
		const std::string detail = "Vanguard\nCreature Attack/Defense/Morale +" + std::to_string(vanguard->statBonus)
			+ "; Initiative +" + std::to_string(vanguard->initiativePercent) + "%"
			+ "\n" + std::to_string(vanguard->roundsRemaining) + " rounds remaining on this stack."
			+ "\nDuration pauses during Time Stop. Morale immunity still applies."
			+ "\nTactical Order: not a spell and cannot be dispelled.";
		result.push_back({HeroCommand::VANGUARD, "Vanguard", detail});
	}
	const auto * battle = stack->getBattle();
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	for(const auto & order : battle->battleGetHeroOrderStates(side))
	{
		const auto * state = &order;
		if(!state->hasScheduledRecipients(battle->battleGetRound()))
			continue;

		const bool own = battle->playerToSide(battle->battleGetOwner(stack)) == side;
		bool applies = false;
		std::string suffix;
		std::string detail;
		switch(state->command)
		{
		case HeroCommand::CHARGE:
			applies = own && !state->containsConsumed(stack->unitId());
			break;
		case HeroCommand::HOLD_THE_LINE:
			if(own)
			{
				const auto * anchor = state->anchorFor(stack->unitId());
				applies = anchor && !state->containsHoldBroken(stack->unitId())
					&& anchor->position == stack->getPosition().toInt();
			}
			break;
		case HeroCommand::FOCUS_FIRE:
			applies = !own && state->primaryTargetUnitId == stack->unitId();
			suffix = "marked target";
			break;
		case HeroCommand::FEINT:
			applies = own && state->revision && battle->battleOrderBenefitAppliesTo(*state, side, stack);
			if(applies)
				detail = "Physical retaliation taken -"
					+ std::to_string(heroCommands::revisedFeintReductionFor(*state, stack->unitId())) + "%";
			break;
		case HeroCommand::RIPOSTE:
			applies = own;
			break;
		case HeroCommand::BRACE:
			applies = own && !state->containsBraceTrigger(stack->unitId());
			break;
		case HeroCommand::PROTECT:
			if(own && !state->protectBroken)
			{
				if(state->primaryTargetUnitId == stack->unitId())
				{
					applies = true;
					suffix = "protector";
				}
				else if(state->secondaryTargetUnitId == stack->unitId())
				{
					applies = true;
					const int limit = battle->battleHeroOrderProtectInterceptionLimit(side);
					const int used = state->protectInterceptionsConsumed;
					suffix = "ward " + std::to_string(used) + "/" + std::to_string(limit);
					detail = "Protect interceptions: " + std::to_string(used) + "/" + std::to_string(limit)
							+ " used; " + std::to_string(std::max(0, limit - used)) + " remaining for this Order.";
				}
			}
			break;
		case HeroCommand::FLANK:
			applies = !own && state->primaryTargetUnitId == stack->unitId();
			suffix = "marked target";
			if(applies)
			{
				const auto * flank = state->flankFor(stack->unitId());
				if(flank)
					detail = "Contributing attack sides relative to this target: "
						+ newHorizonsFlankReadback::formatDirections(flank->sideMask) + ".";
			}
			break;
		case HeroCommand::SECOND_WIND:
			applies = own && state->primaryTargetUnitId == stack->unitId()
				&& !state->containsConsumed(stack->unitId());
			suffix = state->secondWindActive ? "extra activation" : "ready";
			break;
		default:
			break;
		}

		if(own)
			applies = applies && battle->battleOrderBenefitAppliesTo(*state, side, stack);
		else if(applies)
			applies = vstd::contains_if(battle->battleAliveUnits(), [&](const battle::Unit * recipient)
			{
				return battle->battleOrderBenefitAppliesTo(*state, side, recipient);
			});

		if(applies)
		{
			const auto name = orderName(state->command);
			std::string description = name + (suffix.empty() ? "" : " — " + suffix)
				+ (detail.empty() ? "" : "\n" + detail);
			const auto sourceExpiry = newHorizonsOrderBadgeHelp::sourceAndExpiry(
				*state, side, battle->battleGetRound());
			if(!sourceExpiry.empty())
				description += "\n" + sourceExpiry.toString(&GAME->translator());
			description += "\nTactical Order: not a spell and cannot be dispelled.";
			result.push_back({state->command, suffix.empty() ? name : name + ": " + suffix,
				std::move(description)});
		}
	}
	return result;
}
}
