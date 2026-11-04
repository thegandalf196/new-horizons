/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleActionProcessor.h"
#include "HeroOrderLog.h"
#include "../CGameHandler.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/HeroOrderEffects.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/SetStackEffect.h"

bool BattleActionProcessor::doHeroCommandAction(const CBattleInfoCallback & battle, const BattleAction & ba)
{
	// Canonical Orders are represented by an immutable StartAction snapshot and
	// evaluated from that snapshot by the battle callback. Perk bonuses are
	// emitted separately and do not replace the conditional Order state.
	if(heroCommands::isCanonicalRules(battle.getBattle()->getHeroCommandRules()))
	{
		const auto state = battle.getBattle()->getHeroOrderState(ba.side, ba.command);
		if(!state || state->command != ba.command)
			return false;
		const auto grants = heroCommands::prepareIssuedOrderGrants(battle, ba.side, *state);
		for(const auto & batch : grants.removals)
		{
			SetStackEffect update;
			update.battleID = battle.getBattle()->getBattleID();
			update.toRemove = batch;
			gameHandler->sendAndApply(update);
		}
		for(const auto & batch : grants.batches)
		{
			SetStackEffect update;
			update.battleID = battle.getBattle()->getBattleID();
			update.toAdd = batch;
			gameHandler->sendAndApply(update);
		}
		const auto holdFastRecipientCount = grants.holdFastRecipientCount;
		BattleLogMessage message;
		message.battleID = battle.getBattle()->getBattleID();
		message.lines.push_back(heroOrderLogLine(battle, ba.side, *state));
		if(holdFastRecipientCount > 0)
		{
			MetaString holdFastText;
			holdFastText.appendRawString("Hold Fast lets ");
			holdFastText.appendNumber(holdFastRecipientCount);
			holdFastText.appendRawString(holdFastRecipientCount == 1
				? " friendly stack treat negative Morale as 0 until its next Creature Activation."
				: " friendly stacks treat negative Morale as 0 until their next Creature Activation.");
			message.lines.push_back(std::move(holdFastText));
		}
		gameHandler->sendAndApply(message);
		castPassiveDivineMandate(battle, ba.side, *state);
		return true;
	}
	if(ba.command == HeroCommand::FOCUS_FIRE)
		return true; // Validated contextual state was published atomically by StartAction.
	const auto * hero = battle.battleGetFightingHero(ba.side);
	if(!hero || heroCommands::isDoctrine(ba.command)
		|| !heroCommands::supportedByRules(battle.getBattle()->getHeroCommandRules(), ba.command))
		return false;
	const auto effects = heroCommands::bonuses(battle.getBattle()->getHeroCommandRules(), ba.command, *hero);
	SetStackEffect update;
	update.battleID = battle.getBattle()->getBattleID();
	for(const auto * unit : battle.battleGetAllStacks(true))
	{
		if(battle.battleGetOwner(unit) != battle.sideToPlayer(ba.side))
			continue;
		if(unit->alive() && !unit->isTurret() && !unit->hasBonusOfType(BonusType::SIEGE_WEAPON))
			update.toAdd.emplace_back(unit->unitId(), effects);
	}
	gameHandler->sendAndApply(update);
	return true;
}
