/*
 * HypotheticLegacyOrders.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "../../lib/CStack.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

bool HypotheticBattle::beginProjectedLegacyHeroOrder(BattleSide side, HeroCommand command,
	const std::vector<uint32_t> & targets, const ProjectedOrderAllowance & prepared)
{
	if(!isCurrentPreparedOrderAction(side, prepared, false)
		|| heroCommands::isCanonicalRules(getHeroCommandRules())
		|| !battleGetFightingHero(side))
		return false;
	BoundProjectedLegacyOrder bound{this, command, targets, {}};
	if(command == HeroCommand::FOCUS_FIRE)
	{
		if(targets.size() != 1 || !battleCanConfirmHeroCommand(side, command, targets.front()))
			return false;
		bound.focusFire = battlePrepareFocusFireState(side, targets.front());
		if(!bound.focusFire)
			return false;
	}
	else if(!targets.empty() || !battleCanUseHeroCommand(side, command))
		return false;
	if(!beginProjectedHeroAction(side, prepared))
		return false;
	begunProjectedLegacyOrders.at(side) = std::move(bound);
	return true;
}

void HypotheticBattle::applyProjectedLegacyHeroOrder(BattleSide side)
{
	const auto & bound = *begunProjectedLegacyOrders.at(side);
	if(bound.command == HeroCommand::FOCUS_FIRE)
	{
		setFocusFireState(side, *bound.focusFire);
		return;
	}
	// The native saved-rule path and original AI both calculate these bonuses
	// after StartAction cleanup. Never substitute canonical conditional Orders.
	const auto effects = heroCommands::bonuses(getHeroCommandRules(), bound.command,
		*battleGetFightingHero(side));
	for(const auto * unit : battleGetAllStacks(true))
	{
		if(battleGetOwner(unit) != sideToPlayer(side))
			continue;
		if(unit->alive() && !unit->isTurret() && !unit->hasBonusOfType(BonusType::SIEGE_WEAPON))
			addUnitBonus(unit->unitId(), effects);
	}
}
