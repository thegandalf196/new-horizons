/*
 * HeroOrderCandidates.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroOrderCandidates.h"
#include "HeroOrderCandidateRules.h"

#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"

namespace heroOrderCandidates
{
namespace
{
void appendSideWideOrders(std::vector<PossibleSpellcast> & possibleCasts,
	CBattleCallback & callback, BattleID battleID, BattleSide side,
	bool mandatoryOrder, const Valuation & valuation)
{
	// Authoritative queries decide whether an Order allowance is available.
	// Possessing a Spell Action must not hide a separate legal Order Action.
	// Side-wide Orders use the authoritative availability query.  Targeted Orders
	// are enumerated through the callback's legal target-set query, with no local
	// guess about action budget, ownership, or current-round state.
	for(auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE,
		riposteCommand(), braceCommand()})
	{
		const auto battleView = callback.getBattle(battleID);
		if(battleView->battleCanUseHeroCommand(side, command))
		{
			PossibleSpellcast candidate;
			candidate.command = command;
			if(heroCommands::isCanonicalRules(battleView->getBattle()->getHeroCommandRules()))
			{
				candidate.commandHeuristicValue = valuation.score({battleView, side, command, {}, std::nullopt});
				if(!mandatoryOrder && candidate.commandHeuristicValue <= 0.0f)
					continue;
			}
			possibleCasts.push_back(candidate);
		}
	}
}

void appendTargetedOrders(std::vector<PossibleSpellcast> & possibleCasts,
	CBattleCallback & callback, BattleID battleID, BattleSide side,
	bool mandatoryOrder, const Valuation & valuation)
{
	for(auto command : {HeroCommand::FOCUS_FIRE, protectCommand(), flankCommand(), secondWindCommand()})
	{
		for(const auto & targetIds : orderTargetOptions(*callback.getBattle(battleID), side, command))
		{
			if(!commandTargetIsLegal(*callback.getBattle(battleID), side, command, targetIds))
				continue;
			const auto battleView = callback.getBattle(battleID);

			PossibleSpellcast candidate;
			candidate.command = command;
			candidate.commandTargets = targetIds;
			if(command == HeroCommand::FOCUS_FIRE)
			{
				if(targetIds.size() != 1)
					continue;
				candidate.focusFire = callback.getBattle(battleID)->battlePrepareFocusFireState(side, targetIds.front());
				if(!candidate.focusFire)
					continue;
				const auto & recipients = candidate.focusFire->recipientUnitIds;
				const bool hasRemainingShooter = std::any_of(recipients.begin(), recipients.end(), [&](uint32_t id)
				{
					const auto * unit = battleView->battleGetUnitByID(id);
					return unit && unit->willMove(0) && unit->canShoot();
				});
				const bool canonicalRules = heroCommands::isCanonicalRules(
					battleView->getBattle()->getHeroCommandRules());
				// This is the evaluator's own side. Combined Arms may admit a living
				// melee recipient even when no ordinary shooter remains; legacy
				// snapshots retain their historical shooter-only candidate filter.
				const auto * ownHero = canonicalRules ? battleView->battleGetFightingHero(side) : nullptr;
				const bool hasRemainingCombinedArmsMelee = canonicalRules
					&& heroCommands::hasCombinedArms(ownHero)
					&& std::any_of(recipients.begin(), recipients.end(), [&](uint32_t id)
					{
						const auto * unit = battleView->battleGetUnitByID(id);
						return isEligibleOrderUnit(*battleView, side, unit)
							&& unit->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER
							&& unit->willMove(0) && unit->isMeleeAttacker();
					});
				if(!hasRemainingShooter && !hasRemainingCombinedArmsMelee)
					continue;
				candidate.commandHeuristicValue = valuation.score({battleView, side, command, targetIds, candidate.focusFire->rangedDamagePercent});
				if(!mandatoryOrder && candidate.commandHeuristicValue <= 0.0f)
					continue;
			}
			else
			{
				candidate.commandHeuristicValue = valuation.score({battleView, side, command, targetIds, std::nullopt});
				if(!mandatoryOrder && candidate.commandHeuristicValue <= 0.0f)
					continue;
			}
			possibleCasts.push_back(std::move(candidate));
		}
	}
}
}

void appendOrders(std::vector<PossibleSpellcast> & possibleCasts,
	CBattleCallback & callback, BattleID battleID, BattleSide side,
	bool mandatoryOrder, const Valuation & valuation)
{
	appendSideWideOrders(possibleCasts, callback, battleID, side, mandatoryOrder, valuation);
	appendTargetedOrders(possibleCasts, callback, battleID, side, mandatoryOrder, valuation);
}
}
