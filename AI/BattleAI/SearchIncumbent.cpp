/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "SearchIncumbent.h"
#include "HypotheticBattle.h"
#include "PotentialTargets.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsBerserk.h"
#include "../../lib/battle/NewHorizonsPuppetMaster.h"
namespace battleSearch
{
namespace
{
bool continuationOnly(const CStack & stack)
{
	return stack.rangedFollowUpDamagePercent > 0 || stack.pursuitMovementRemaining > 0;
}
BattleAction forcedAction(const CStack * stack, const ForcedAction & forced)
{
	if(forced.type == EActionType::WALK_AND_ATTACK && forced.target)
		return BattleAction::makeMeleeAttack(stack, forced.target->getPosition(), forced.position);
	if(forced.type == EActionType::SHOOT && forced.target) return BattleAction::makeShotAttack(stack, forced.target);
	if(forced.type == EActionType::WALK) return BattleAction::makeMove(stack, forced.position);
	return BattleAction::makeNoAction(stack);
}
}
BattleAction visibleIncumbent(const Environment * environment,
	const std::shared_ptr<CPlayerBattleCallback> & view, const CStack * stack)
{
	if(continuationOnly(*stack) || !stack->canMove()) return BattleAction::makeNoAction(stack);
	if(!stack->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE)
		|| newHorizonsPuppetMaster::hasValidControlMarker(*view, stack)) return BattleAction::makeDefend(stack);
	// The ordinary forced-action query must see Frenzied Curse's scoped Speed.
	// This mandatory, single legality query precedes optional work budgets. The
	// detached view and wire action retain no live-unit mutations or borrowed aim.
	std::shared_ptr<HypotheticBattle> forcedView;
	const CBattleInfoCallback * board = view.get();
	const battle::Unit * actor = stack;
	if(const auto bonus = newHorizonsBerserk::forcedActivationSpeedBonus(*view, stack))
	{
		forcedView = std::make_shared<HypotheticBattle>(environment, view);
		forcedView->addUnitBonus(stack->unitId(), {*bonus});
		board = forcedView.get();
		actor = forcedView->battleGetUnitByID(stack->unitId());
	}
	return forcedAction(stack, board->getBerserkForcedAction(actor));
}

BattleAction preparedIncumbent(const CStack * stack, const PotentialTargets & targets)
{
	if(continuationOnly(*stack) || !stack->canMove()) return BattleAction::makeNoAction(stack);
	if(targets.berserk) return targets.forcedBerserkActions.empty()
		? BattleAction::makeNoAction(stack) : forcedAction(stack, targets.forcedBerserkActions.front());
	if(targets.possibleAttacks.empty()) return BattleAction::makeDefend(stack);
	const auto & attack = targets.bestAction();
	auto action = attack.attack.shooting ? BattleAction::makeShotAttack(stack, attack.attack.defender)
		: BattleAction::makeMeleeAttack(stack, attack.attack.defenderPos, attack.from);
	if(attack.attack.shooting && attack.from.isValid() && attack.from != stack->getPosition())
	{
		action = BattleAction::makeMeleeAttack(stack, attack.attack.defenderPos, attack.from, false);
		action.archerySkirmisherAttack = true;
	}
	action.perfectMoment = attack.perfectMoment;
	return action;
}
}
