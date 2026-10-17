/*
 * FocusFireQueries.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "FocusFireQueries.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "SideInBattle.h"
#include "Unit.h"

namespace
{
bool legacyFocusFireRules(const JsonNode & rules)
{
	return rules.isStruct() && rules["rulesetVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& rules["rulesetVersion"].Integer() == heroCommands::TARGETED_RULESET_VERSION
		&& heroCommands::supportedByRules(rules, HeroCommand::FOCUS_FIRE);
}

bool scheduledFocusFire(const JsonNode & rules, const FocusFireState & mark,
	const std::optional<HeroOrderState> & order, int round, std::optional<uint32_t> unitId)
{
	if(heroCommands::isCanonicalRules(rules))
		return order && (unitId ? order->scheduledFor(*unitId, round) : order->hasScheduledRecipients(round));
	// Legacy v2 snapshots have a round-owned mark and no canonical Order record.
	return legacyFocusFireRules(rules) && mark.issuedRound == round;
}
}

bool CBattleInfoCallback::battleIsFocusFireTargetActive(BattleSide side) const
{
	const auto mark = battleGetFocusFireState(side);
	const auto order = battleGetHeroOrderState(side, HeroCommand::FOCUS_FIRE);
	if(!mark || !scheduledFocusFire(getBattle()->getHeroCommandRules(), *mark, order, battleGetRound(), std::nullopt))
		return false;
	const auto * target = battleGetUnitByID(mark->targetUnitId);
	return target && target->alive() && !target->isGhost() && battleGetOwner(target) != sideToPlayer(side);
}

bool CBattleInfoCallback::battleIsTargetedRangedCommand(const battle::Unit * attacker,
	const battle::Unit * defender, bool shooting, bool secondaryAttack) const
{
	if(!shooting || secondaryAttack || !getBattle() || !attacker || !defender || !attacker->alive() || attacker->isGhost()
		|| !defender->alive() || defender->isGhost() || battleGetOwner(attacker) == battleGetOwner(defender))
		return false;
	const auto side = playerToSide(battleGetOwner(attacker));
	if(!battleIsFocusFireRecipient(attacker, side))
		return false;
	const auto mark = battleGetFocusFireState(side);
	const auto order = battleGetHeroOrderState(side, HeroCommand::FOCUS_FIRE);
	return mark && scheduledFocusFire(getBattle()->getHeroCommandRules(), *mark, order, battleGetRound(), attacker->unitId())
		&& mark->targetUnitId == defender->unitId()
		&& std::binary_search(mark->recipientUnitIds.begin(), mark->recipientUnitIds.end(), attacker->unitId());
}

int CBattleInfoCallback::battleTargetedRangedCommandPercent(const battle::Unit * attacker,
	const battle::Unit * defender, bool shooting, bool secondaryAttack) const
{
	if(!battleIsTargetedRangedCommand(attacker, defender, shooting, secondaryAttack))
		return 0;
	const auto side = playerToSide(battleGetOwner(attacker));
	const auto mark = battleGetFocusFireState(side);
	const auto order = battleGetHeroOrderState(side, HeroCommand::FOCUS_FIRE);
	if(legacyFocusFireRules(getBattle()->getHeroCommandRules()))
		return mark->rangedDamagePercent;
	const int value = order && order->crownAndAltarAppliesTo(attacker->unitId())
		? order->crownAndAltarFocusFirePercent : mark->rangedDamagePercent;
	return order ? heroCommands::orderPotency(mark->rangedDamagePercent, value,
		*order, getBattle()->getMagicRules()) : value;
}

void focusFireQueries::validateMarkContext(const CBattleInfoCallback & battle, BattleSide side,
	const SideInBattle & state, bool canonicalOrderRules)
{
	const bool focusFireOrderExists = state.findOrder(HeroCommand::FOCUS_FIRE) != nullptr;
	const bool focusFireOrderExpected = canonicalOrderRules
		? focusFireOrderExists : state.activeOrder == HeroCommand::FOCUS_FIRE;
	if(state.focusFire.has_value() != focusFireOrderExpected)
		throw std::runtime_error("Inconsistent New Horizons Focus Fire order state");
	if(!state.focusFire)
		return;
	const auto & mark = *state.focusFire;
	mark.validateShape();
	const auto * order = state.findOrder(HeroCommand::FOCUS_FIRE);
	const bool validLifetime = canonicalOrderRules
		? order && mark.issuedRound == order->issuedRound
		: legacyFocusFireRules(battle.getBattle()->getHeroCommandRules())
			&& mark.issuedRound == battle.battleGetRound();
	if(!heroCommands::supportedByRules(battle.getBattle()->getHeroCommandRules(), HeroCommand::FOCUS_FIRE)
		|| !battle.getBattle()->getSideHero(side) || !validLifetime
		|| !battle.battleGetUnitByID(mark.targetUnitId))
		throw std::runtime_error("Invalid New Horizons Focus Fire battle context");
	for(auto id : mark.recipientUnitIds)
	{
		// Ghosts and changed controllers are valid retained, possibly inactive references.
		if(!battle.battleGetUnitByID(id))
			throw std::runtime_error("Invalid New Horizons Focus Fire recipient reference");
	}
}
