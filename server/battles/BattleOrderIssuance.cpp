/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleActionProcessor.h"
#include "HeroOrderLog.h"
#include "../../lib/battle/NewHorizonsWarcast.h"
#include "../CGameHandler.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/HeroOrderEffects.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/SetStackEffect.h"

namespace
{
/// One plain combat-log line for a warcast Order, so players and tests see the bonus.
void appendWarcastLog(const CBattleInfoCallback & battle, BattleSide side, const HeroOrderState & state,
	size_t sustained, BattleLogMessage & message)
{
	if(state.warcast.recipients.empty())
		return;
	int best = 0;
	for(const auto & entry : state.warcast.recipients)
		best = std::max<int>(best, entry.percent);
	MetaString line;
	const auto * hero = battle.battleGetFightingHero(side);
	if(hero)
		line.appendTextID(hero->getNameTextID());
	else
		line.appendRawString("Hero");
	line.appendRawString(": Warcasting strengthens ");
	line.appendRawString(heroOrderDisplayName(state.command));
	line.appendRawString("! by up to ");
	line.appendNumber(best);
	line.appendRawString(state.warcast.recipients.size() == 1 ? "% on 1 warcast stack" : "% on ");
	if(state.warcast.recipients.size() != 1)
	{
		line.appendNumber(static_cast<int64_t>(state.warcast.recipients.size()));
		line.appendRawString(" warcast stacks");
	}
	if(state.warcast.meditation)
		line.appendRawString(", and Commander's Meditation restores 3 Mana");
	if(sustained > 0)
	{
		line.appendRawString(state.warcast.meditation ? "; Sustaining Command extends " : ", and Sustaining Command extends ");
		line.appendNumber(static_cast<int64_t>(sustained));
		line.appendRawString(sustained == 1 ? " spell by one round" : " spells by one round");
	}
	line.appendRawString(".");
	message.lines.push_back(std::move(line));
}
}

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
		appendWarcastLog(battle, ba.side, *state, grants.sustainedEffectCount, message);
		gameHandler->sendAndApply(message);
		if(state->warcast.meditation)
			if(const auto * hero = battle.battleGetFightingHero(ba.side))
				gameHandler->restoreSpellPoints(hero->id, newHorizonsWarcast::MEDITATION_MANA);
		castPassiveDivineMandate(battle, ba, *state);
		if(ba.command == HeroCommand::FOCUS_FIRE)
			performVolleyCommand(battle, ba.side);
		return true;
	}
	if(ba.command == HeroCommand::FOCUS_FIRE)
	{
		// Validated contextual state was published atomically by StartAction.
		performVolleyCommand(battle, ba.side);
		return true;
	}
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
