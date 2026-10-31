/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleActionProcessor.h"
#include "BattleMaintenanceRecovery.h"
#include "BattlecraftMasteryAward.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/NewHorizonsArmorer.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsDiscipline.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/SetStackEffect.h"
bool BattleActionProcessor::doDefendAction(const CBattleInfoCallback & battle, const BattleAction & ba)
{
	const CStack * stack = battle.battleGetStackByID(ba.stackNumber);

	if (!canStackAct(battle, stack))
		return false;
	return applyDefendStance(battle, stack, true);
}

bool BattleActionProcessor::applyDefendStance(const CBattleInfoCallback & battle, const CStack * stack,
	bool voluntary)
{
	if(!stack || !stack->alive())
		return false;

	//defensive stance, TODO: filter out spell boosts from bonus (stone skin etc.)
	SetStackEffect sse;
	sse.battleID = battle.getBattle()->getBattleID();
	const auto * defendingHero = battle.battleGetOwnerHero(stack);
	const bool holdFastApplies = heroCommands::isCanonicalRules(battle.getBattle()->getHeroCommandRules())
		&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(stack)
		&& newHorizonsDiscipline::hasHoldFast(defendingHero);
	const auto stance = newHorizonsArmorer::buildDefendStance(stack, holdFastApplies);
	sse.toUpdate.emplace_back(stack->unitId(), stance.bonuses);
	if(stance.holdFastBonus)
		sse.toAdd.emplace_back(stack->unitId(),
			std::vector<Bonus>{*stance.holdFastBonus});
	gameHandler->sendAndApply(sse);

	// Publish the explicit provenance alongside the bonuses.  This is a state
	// update, not a client-side mutation, and therefore survives save/load and
	// hypothetical battle copies just like the rest of CUnitState.
	auto state = stack->acquireState();
	state->defensiveStanceMeleeBonus = stance.meleeDefenseBonus;
	state->defensiveStanceRangedBonus = stance.rangedDefenseBonus;
	if(!voluntary)
		state->armorerLastStandDefending = true;
	BattleUnitsChanged stateChanged;
	stateChanged.battleID = battle.getBattle()->getBattleID();
	UnitChanges stateUpdate(stack->unitId(), UnitChanges::EOperation::UPDATE);
	stateUpdate.data = state->save();
	stateChanged.changedStacks.push_back(std::move(stateUpdate));
	gameHandler->sendAndApply(stateChanged);
	battleMaintenanceRecovery::onDefend(*gameHandler, battle, *stack, voluntary && voluntaryMovementAction);

	if(voluntary)
		battlecraftMasteryAward::apply(*gameHandler, battle, stack, BattlecraftMasteryAction::DEFEND);

	BattleLogMessage message;
	message.battleID = battle.getBattle()->getBattleID();

	MetaString text;
	stack->addText(text, EMetaText::GENERAL_TXT, 120);
	stack->addNameReplacement(text);
	text.replaceNumber(stance.defenseIncrease);

	message.lines.push_back(text);
	if(newHorizonsCombatSkills::paviseReductionPercent(battle.battleGetOwnerHero(stack)) > 0)
	{
		MetaString paviseText;
		paviseText.appendRawString("%s braces behind a Pavise, reducing ranged physical creature damage by 25% while Defending.");
		stack->addNameReplacement(paviseText);
		message.lines.push_back(std::move(paviseText));
	}
	if(stance.holdFastApplied)
	{
		MetaString holdFastText;
		holdFastText.appendRawString("Hold Fast lets %s treat negative Morale as 0 until its next Creature Activation.");
		stack->addNameReplacement(holdFastText);
		message.lines.push_back(std::move(holdFastText));
	}

	if(!voluntary)
	{
		MetaString line;
		line.appendRawString("Last Stand leaves %s at 1 HP and immediately puts it into a defensive stance.");
		stack->addNameReplacement(line);
		message.lines.push_back(std::move(line));
	}
	gameHandler->sendAndApply(message);

	processBattleEventTriggers(battle, CombatEventType::DEFEND, stack, nullptr);
	return true;
}
