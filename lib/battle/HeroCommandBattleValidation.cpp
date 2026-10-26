/*
 * HeroCommandBattleValidation.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleInfo.h"
#include "FocusFireQueries.h"
#include "NewHorizonsDivineMandate.h"
#include "NewHorizonsIronWill.h"
#include "../CStack.h"
#include "../mapObjects/CGHeroInstance.h"

void BattleInfo::validateFocusFireStates() const
{
	std::set<uint32_t> unitIds;
	const bool targetedRules = heroCommands::supportedByRules(heroCommandRules, HeroCommand::FOCUS_FIRE);
	for(const auto & unit : stacks)
	{
		if(!unit)
			throw std::runtime_error("Invalid null battle unit reference");
		// nextUnitId allocates by vector size. Unique IDs below size imply dense
		// coverage even after sorting; removed units remain as retained descriptors.
		if(targetedRules && (unit->unitId() > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
			|| unit->unitId() >= stacks.size() || !unitIds.insert(unit->unitId()).second))
			throw std::runtime_error("Invalid New Horizons targeted battle unit identity");
	}
	for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto & state = sides.at(side);
		state.validateOrderStates();
		const bool canonicalOrderRules = heroCommands::isCanonicalRules(heroCommandRules);
		// In legacy saves the spell count doubled as an action-budget marker.
		// Once the typed ledger is initialized for this round, spell history is
		// descriptive only and must not invalidate a later Order.
		const bool legacySpellHistoryBlocksOrder = state.castSpellsCount != 0
			&& (!heroCommands::supportedByRules(heroCommandRules, HeroCommand::CHARGE)
				|| state.heroActionAllowances.currentRound != round);
		if((state.activeDoctrine != HeroCommand::NONE
				&& (!heroCommands::isDoctrine(state.activeDoctrine)
					|| !heroCommands::supportedByRules(heroCommandRules, state.activeDoctrine)))
			|| (state.activeOrder != HeroCommand::NONE
				&& (heroCommands::isDoctrine(state.activeOrder)
					|| !heroCommands::supportedByRules(heroCommandRules, state.activeOrder)
					|| ((state.heroCommandUsed || state.orderStates.empty()
						|| state.orderStates.back().issuedRound == round)
						&& (!state.heroCommandUsed || legacySpellHistoryBlocksOrder)))))
			throw std::runtime_error("Invalid New Horizons saved command state");
		if(!state.orderStates.empty())
		{
			if(!canonicalOrderRules
				|| state.orderStates.back().command != state.activeOrder
				|| (state.orderStates.back().issuedRound == round
					&& (!state.heroCommandUsed || legacySpellHistoryBlocksOrder)))
				throw std::runtime_error("Invalid New Horizons canonical Order context");
			for(const auto & order : state.orderStates)
			{
				if(!order.crownAndAltarRecipientUnitIds.empty())
				{
					if(!newHorizonsDivineMandate::hasCrownAndAltarPerk(getSideHero(side)))
						throw std::runtime_error("Invalid Crown and Altar captured Order context");
					for(const auto id : order.crownAndAltarRecipientUnitIds)
						if(!battleGetUnitByID(id))
							throw std::runtime_error("Crown and Altar references a missing recipient");
				}
				if(!order.divineDisciplineRecipientUnitIds.empty())
				{
					if((order.ironWillLifetime ? !newHorizonsIronWill::hasPerk(getSideHero(side))
						: !newHorizonsDivineMandate::hasDivineDisciplinePerk(getSideHero(side)))
						|| (order.issuedRound == round && !order.divineDisciplineCompletedUnitIds.empty()))
						throw std::runtime_error("Invalid Divine Discipline captured Order context");
					for(const auto id : order.divineDisciplineRecipientUnitIds)
						if(!battleGetUnitByID(id))
							throw std::runtime_error("Divine Discipline references a missing recipient");
				}
				if((order.command == HeroCommand::VANGUARD ? !order.hasScheduledRecipients(round)
					: (order.issuedRound != round && order.divineDisciplineRecipientUnitIds.empty()))
					|| order.issuedRound > round
					|| !heroCommands::supportedByRules(heroCommandRules, order.command))
					throw std::runtime_error("Invalid New Horizons canonical Order context");
				if(order.revision && !order.revision->potencyFinalized)
					throw std::runtime_error("Installed revised Order potency is not finalized");
				if(order.revision)
					for(const auto id : order.revision->recipientUnitIds)
						if(!battleGetUnitByID(id))
							throw std::runtime_error("Vanguard references a missing recipient");
				if(order.primaryTargetUnitId != HeroOrderState::INVALID_UNIT_ID
					&& !battleGetUnitByID(order.primaryTargetUnitId))
					throw std::runtime_error("Invalid New Horizons canonical Order target");
				if(order.secondaryTargetUnitId != HeroOrderState::INVALID_UNIT_ID
					&& !battleGetUnitByID(order.secondaryTargetUnitId))
					throw std::runtime_error("Invalid New Horizons canonical Order secondary target");
				if(order.command == HeroCommand::PROTECT
					&& order.protectInterceptionsConsumed > battleHeroOrderProtectInterceptionLimit(side))
					throw std::runtime_error("Shield Master Protect interception count exceeds the saved hero perk limit");
			}
		}
		else if(canonicalOrderRules && state.activeOrder != HeroCommand::NONE)
			throw std::runtime_error("Missing New Horizons canonical Order state");
		focusFireQueries::validateMarkContext(*this, side, state, canonicalOrderRules);
	}
}
